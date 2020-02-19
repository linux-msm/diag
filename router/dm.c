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

	bool hdlc_encoded;

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
struct diag_client *dm_add(const char *name, int in_fd, int out_fd, bool hdlc_encoded)
{
	struct diag_client *dm;

	dm = calloc(1, sizeof(*dm));
	if (!dm)
		err(1, "failed to allocate DM context\n");

	dm->name = strdup(name);
	dm->in_fd = in_fd;
	dm->out_fd = out_fd;
	dm->hdlc_encoded = hdlc_encoded;
	list_init(&dm->outq);

	if (dm->in_fd >= 0)
		watch_add_readfd(dm->in_fd, dm_recv, dm, NULL);
	watch_add_writeq(dm->out_fd, &dm->outq);

	list_add(&diag_clients, &dm->node);

	/* Disable DM by default, so that  */
	dm->enabled = false;

	return dm;
}

static int dm_recv_hdlc(struct diag_client *dm)
{
	size_t msglen;
	ssize_t n;
	void *msg;
	int ret = 0;

	for (;;) {
		n = circ_read(dm->in_fd, &dm->recv_buf);
		if (n < 0 && errno == EAGAIN) {
			break;
		} else if (n < 0) {
			ret = -errno;
			warn("Failed to read from %s\n", dm->name);
			break;
		}

		for (;;) {
			msg = hdlc_decode_one(&dm->recv_decoder, &dm->recv_buf,
					      &msglen);
			if (!msg)
				break;

			diag_client_handle_command(dm, msg, msglen);
		}
	}

	return ret;
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

/**
 * dm_recv() - read and handle data from a DM
 * @fd:		the file descriptor associated with the DM
 * @data:	private data, must be a diag_client object
 */
int dm_recv(int fd, void* data)
{
	struct diag_client *dm = (struct diag_client *)data;

	if (dm->hdlc_encoded)
		return dm_recv_hdlc(dm);
	else
		return dm_recv_raw(dm);
}

static ssize_t dm_send_flow(struct diag_client *dm, const void *ptr, size_t len,
			    struct watch_flow *flow)
{
	if (!dm->enabled)
		return 0;

	if (dm->hdlc_encoded)
		hdlc_enqueue_flow(&dm->outq, ptr, len, flow);
	else
		queue_push_flow(&dm->outq, ptr, len, flow);

	return 0;
}

/**
 * dm_send() - enqueue message to DM
 * @dm:		dm to be receiving the message
 * @ptr:	pointer to raw message to be sent
 * @len:	length of message
 */
ssize_t dm_send(struct diag_client *dm, const void *ptr, size_t len)
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
