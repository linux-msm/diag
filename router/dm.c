/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "diag.h"
#include "dm.h"
#include "watch.h"

/**
 * DOC: Diagnostic Monitor
 */

struct diag_client {
	const char *name;
	int fd;
	int in_fd;
	int out_fd;

	int encode_type;
	bool encode_hdlc_reset;

	bool enabled;

	struct circ_buf recv_buf;
	struct hdlc_decoder recv_decoder;

	struct list_head outq;
	struct list_head node;
};

struct list_head diag_clients = LIST_INIT(diag_clients);

/**
 * dm_add() - register new DM
 * @dm:		DM object to register
 */
struct diag_client *dm_add(const char *name, int in_fd, int out_fd, bool is_encoded)
{
	struct diag_client *dm;

	dm = calloc(1, sizeof(*dm));
	if (!dm)
		err(1, "failed to allocate DM context\n");

	dm->name = strdup(name);
	dm->in_fd = in_fd;
	dm->out_fd = out_fd;
	dm->encode_type = (is_encoded) ? DIAG_ENCODE_HDLC : DIAG_ENCODE_RAW;
	list_init(&dm->outq);

	if (dm->in_fd >= 0)
		watch_add_readfd(dm->in_fd, dm_recv, dm, NULL);
	watch_add_writeq(dm->out_fd, &dm->outq);

	list_add(&diag_clients, &dm->node);

	/* Disable DM by default, so that  */
	dm->enabled = false;

	return dm;
}

static int dm_recv_hdlc(struct diag_client *dm, struct circ_buf *buf)
{
	struct hdlc_decoder recv_decoder = {0};
	size_t msglen;
	void *msg;

	for (;;) {
		msg = hdlc_decode_one(&recv_decoder, buf, &msglen);
		if (!msg)
			break;

		diag_client_handle_command(dm, msg, msglen);
	}

	return 0;
}

static int dm_recv_raw(struct diag_client *dm)
{
	int saved_errno;
	unsigned char buf[4096];
	ssize_t n;

	for (;;) {
		n = read(dm->in_fd, buf, sizeof(buf));
		if (!n) {
			watch_remove_fd(dm->in_fd);
			break;
		} else if (n < 0 && errno == EAGAIN) {
			break;
		} else if (n < 0) {
			saved_errno = -errno;
			warn("Failed to read from %s\n", dm->name);
			return saved_errno;
		}

		diag_client_handle_command(dm, buf, n);
	}

	return 0;
}

static void diag_hdlc_reset(void *data)
{
	struct diag_client *dm = (struct diag_client *)data;

	set_encode_type(DIAG_ENCODE_HDLC);
	dm->encode_hdlc_reset = false;
}

#define DIAG_MAX_BAD_CMD	5
static int diag_start_hdlc_recovery(struct diag_client *dm)
{
	static uint32_t bad_cmd_counter;

	if (!dm->encode_hdlc_reset) {
		watch_add_timer(diag_hdlc_reset, (void *)dm, 200, 0);
		dm->encode_hdlc_reset = true;
	}

	bad_cmd_counter++;
	if (bad_cmd_counter > DIAG_MAX_BAD_CMD) {
		bad_cmd_counter = 0;
		dm->encode_type = DIAG_ENCODE_HDLC;
	}

	return 0;
}

static int dm_check_nhdlc_pkt(struct diag_client *dm, struct diag_pkt_frame *pkt_ptr)
{
	if (pkt_ptr->start != NHDLC_CONTROL_CHAR ||
		 *(uint8_t *)(pkt_ptr->data + pkt_ptr->length) != NHDLC_CONTROL_CHAR) {
		warn("Diag: pkt is not correct, %s\n", dm->name);
		return -EINVAL;
	}

	return 0;
}

static int dm_recv_nhdlc(struct diag_client *dm, struct circ_buf *buf)
{
	struct diag_pkt_frame *pkt_ptr;
	int ret;

	pkt_ptr = (struct diag_pkt_frame *)buf;
	ret = dm_check_nhdlc_pkt(dm, pkt_ptr);
	if (ret)
		diag_start_hdlc_recovery(dm);
	diag_client_handle_command(dm, &pkt_ptr->data[0], pkt_ptr->length);

	return ret;
}

int dm_decode_data(struct diag_client *dm, struct circ_buf *buf)
{
	switch (dm->encode_type) {
	case DIAG_ENCODE_HDLC:
		return dm_recv_hdlc(dm, buf);
	case DIAG_ENCODE_NHDLC:
		return dm_recv_nhdlc(dm, buf);
	default:
		warn("Diag: recv error encode type %d\n", dm->encode_type);
		return -EINVAL;
	}
}

static int dm_recv_encoded(struct diag_client *dm)
{
	ssize_t n;

	n = circ_read(dm->in_fd, &dm->recv_buf);
	if (n < 0 && errno != EAGAIN) {
		warn("Failed to read from %s\n", dm->name);
		return -errno;
	}

	return dm_decode_data(dm, &dm->recv_buf);
}

/**
 * dm_recv() - read and handle data from a DM
 * @fd:		the file descriptor associated with the DM
 * @data:	private data, must be a diag_client object
 */
int dm_recv(int fd, void* data)
{
	struct diag_client *dm = (struct diag_client *)data;

	if (!dm)
		return -EINVAL;

	switch (dm->encode_type) {
	case DIAG_ENCODE_RAW:
		return dm_recv_raw(dm);
	case DIAG_ENCODE_HDLC:
	case DIAG_ENCODE_NHDLC:
		return dm_recv_encoded(dm);
	default:
		warn("Diag: recv error encode type %d\n", dm->encode_type);
		break;
	}

	return -EINVAL;
}

static int dm_send_flow(struct diag_client *dm, const void *ptr, size_t len,
			    struct watch_flow *flow)
{
	if (dm && !dm->enabled)
		return 0;

	switch (dm->encode_type) {
	case DIAG_ENCODE_RAW:
		queue_push_flow(&dm->outq, ptr, len, flow);
		break;
	case DIAG_ENCODE_HDLC:
		hdlc_enqueue_flow(&dm->outq, ptr, len, flow);
		break;
	case DIAG_ENCODE_NHDLC:
		nhdlc_enqueue_flow(&dm->outq, ptr, len, flow);
		break;
	default:
		warn("Diag: send error encode type %d\n", dm->encode_type);
		return -EINVAL;
	}

	return 0;
}

/**
 * dm_send() - enqueue message to DM
 * @dm:		dm to be receiving the message
 * @ptr:	pointer to raw message to be sent
 * @len:	length of message
 */
int dm_send(struct diag_client *dm, const void *ptr, size_t len)
{
	return dm_send_flow(dm, ptr, len, NULL);
}

/**
 * dm_broadcast() - send message to all registered DMs
 * @ptr:	pointer to raw message to be sent
 * @len:	length of message
 * @flow:	flow control context for the peripheral
 */
void dm_broadcast(const void *ptr, size_t len, struct watch_flow *flow)
{
	struct diag_client *dm;
	struct list_head *item;

	list_for_each(item, &diag_clients) {
		dm = container_of(item, struct diag_client, node);

		dm_send_flow(dm, ptr, len, flow);
	}
}

void dm_enable(struct diag_client *dm)
{
	dm->enabled = true;
}

void dm_disable(struct diag_client *dm)
{
	dm->enabled = false;

	/* XXX: purge dm->outq */
}

void set_encode_type(int type)
{
	struct diag_client *dm = NULL;

	list_for_each_entry(dm, &diag_clients, node) {
		if (dm->encode_type == DIAG_ENCODE_RAW)
			continue;
		dm->encode_type = type;
	}
}
