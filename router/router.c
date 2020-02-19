/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016, Linaro Ltd.
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "dm.h"
#include "hdlc.h"
#include "peripheral.h"
#include "util.h"

#define DIAG_CMD_RSP_BAD_COMMAND			0x13
#define DIAG_CMD_RSP_BAD_PARAMS				0x14
#define DIAG_CMD_RSP_BAD_LENGTH				0x15

struct list_head fallback_cmds = LIST_INIT(fallback_cmds);
struct list_head common_cmds = LIST_INIT(common_cmds);

int hdlc_enqueue_flow(struct list_head *queue, const void *msg, size_t msglen,
		      struct watch_flow *flow)
{
	size_t outlen;
	void *outbuf;

	outbuf = hdlc_encode(msg, msglen, &outlen);
	if (!outbuf)
		err(1, "failed to allocate hdlc destination buffer");

	queue_push_flow(queue, outbuf, outlen, flow);
	free(outbuf);

	return 0;
}

int hdlc_enqueue(struct list_head *queue, const void *msg, size_t msglen)
{
	return hdlc_enqueue_flow(queue, msg, msglen, NULL);
}

static int diag_cmd_dispatch(struct diag_client *client, uint8_t *ptr,
			     size_t len)
{
	struct list_head *item;
	struct diag_cmd *dc;
	unsigned int key;
	int handled = 0;

	if (ptr[0] == DIAG_CMD_SUBSYS_DISPATCH ||
	    ptr[0] == DIAG_CMD_SUBSYS_DISPATCH_V2)
		key = ptr[0] << 24 | ptr[1] << 16 | ptr[3] << 8 | ptr[2];
	else
		key = 0xff << 24 | 0xff << 16 | ptr[0];

	list_for_each(item, &common_cmds) {
		dc = container_of(item, struct diag_cmd, node);
		if (key < dc->first || key > dc->last)
			continue;

		return dc->cb(client, ptr, len);
	}

	list_for_each(item, &diag_cmds) {
		dc = container_of(item, struct diag_cmd, node);
		if (key < dc->first || key > dc->last)
			continue;

		if (dc->cb)
			dc->cb(client, ptr, len);
		else
			peripheral_send(dc->peripheral, ptr, len);

		handled++;
	}

	if (handled)
		return 0;

	list_for_each_entry(dc, &fallback_cmds, node) {
		if (key < dc->first || key > dc->last)
			continue;

		return dc->cb(client, ptr, len);
	}

	return -ENOENT;
}

static void diag_rsp_bad_command(struct diag_client *client, uint8_t *msg,
				 size_t len, int error_code)
{
	uint8_t *buf;

	buf = malloc(len + 1);
	if (!buf)
		err(1, "failed to allocate error buffer");

	buf[0] = error_code;
	memcpy(buf + 1, msg, len);

	dm_send(client, buf, len + 1);

	free(buf);
}

int diag_client_handle_command(struct diag_client *client, uint8_t *data, size_t len)
{
	int ret;

	ret = diag_cmd_dispatch(client, data, len);

	switch (ret) {
	case -ENOENT:
		diag_rsp_bad_command(client, data, len, DIAG_CMD_RSP_BAD_COMMAND);
		break;
	case -EINVAL:
		diag_rsp_bad_command(client, data, len, DIAG_CMD_RSP_BAD_PARAMS);
		break;
	case -EMSGSIZE:
		diag_rsp_bad_command(client, data, len, DIAG_CMD_RSP_BAD_LENGTH);
		break;
	default:
		break;
	}

	return 0;
}

void register_fallback_cmd(unsigned int cmd,
			   int(*cb)(struct diag_client *client,
				    const void *buf, size_t len))
{
	struct diag_cmd *dc;
	unsigned int key = 0xffff0000 | cmd;

	dc = calloc(1, sizeof(*dc));
	if (!dc)
		err(1, "failed to allocate diag command\n");

	dc->first = key;
	dc->last = key;
	dc->cb = cb;

	list_add(&fallback_cmds, &dc->node);
}

void register_fallback_subsys_cmd(unsigned int subsys, unsigned int cmd,
				  int(*cb)(struct diag_client *client,
					   const void *buf, size_t len))
{
	struct diag_cmd *dc;
	unsigned int key = DIAG_CMD_SUBSYS_DISPATCH << 24 |
			   (subsys & 0xff) << 16 | cmd;

	dc = calloc(1, sizeof(*dc));
	if (!dc)
		err(1, "failed to allocate diag command\n");

	dc->first = key;
	dc->last = key;
	dc->cb = cb;

	list_add(&fallback_cmds, &dc->node);
}

void register_common_cmd(unsigned int cmd, int(*cb)(struct diag_client *client,
						    const void *buf,
						    size_t len))
{
	struct diag_cmd *dc;
	unsigned int key = 0xffff0000 | cmd;

	dc = calloc(1, sizeof(*dc));
	if (!dc)
		err(1, "failed to allocate diag command\n");

	dc->first = key;
	dc->last = key;
	dc->cb = cb;

	list_add(&common_cmds, &dc->node);
}
