/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "diag.h"
#include "hdlc.h"
#include "masks.h"
#include "mbuf.h"
#include "peripheral.h"
#include "util.h"
#include "watch.h"

struct list_head diag_cmds = LIST_INIT(diag_cmds);

static size_t iovec_total_len(struct iovec *iov, int iovcnt)
{
	size_t total = 0;

	for (int i = 0; i < iovcnt; i++) {
		total += iov[i].iov_len;
	}

	return total;
}

void queue_push_sg_flow(struct list_head *queue, struct iovec *iov, int iovcnt,
			 struct watch_flow *flow)
{
	struct mbuf *mbuf;
	void *ptr, *src;
	size_t iovec_len, len;
	size_t offset = 0;

	iovec_len = iovec_total_len(iov, iovcnt);
	mbuf = mbuf_alloc(sizeof(*mbuf) + iovec_len);
	if (!mbuf) {
		warnx("Diag: %s: failed to allocate memory", __func__);
		return;
	}

	ptr = mbuf_put(mbuf, sizeof(*mbuf) + iovec_len);
	if (!ptr) {
		warnx("Diag: invalid ptr, dropping pkt of len: %zu\n", sizeof(*mbuf) + iovec_len);
		free(mbuf);
		return;
	}

	for (int i = 0; i < iovcnt; ++i) {
		src = iov[i].iov_base;
		len = iov[i].iov_len;
		memcpy(ptr + offset, src, len);
		offset += len;
	}

	mbuf->offset = offset;
	mbuf->size = iovec_len;
	mbuf->flow = flow;

	watch_flow_inc(flow);
	list_add(queue, &mbuf->node);
}

void queue_push_flow(struct list_head *queue, const void *msg, size_t msglen,
		     struct watch_flow *flow)
{
	struct mbuf *mbuf;
	void *ptr;

	mbuf = mbuf_alloc(msglen);
	ptr = mbuf_put(mbuf, msglen);
	memcpy(ptr, msg, msglen);

	mbuf->flow = flow;

	watch_flow_inc(flow);

	list_add(queue, &mbuf->node);
}

void queue_push(struct list_head *queue, const void *msg, size_t msglen)
{
	queue_push_flow(queue, msg, msglen, NULL);
}

static void usage(void)
{
	fprintf(stderr,
		"User space application for diag interface\n"
		"\n"
		"usage: diag [-hsu]\n"
		"\n"
		"options:\n"
		"   -h   show this usage\n"
		"   -s   <socket address[:port]>\n"
		"   -u   <uart device name[@baudrate]>\n"
	);

	exit(1);
}

int main(int argc, char **argv)
{
	char *host_address = NULL;
	int host_port = DEFAULT_SOCKET_PORT;
	char *uartdev = NULL;
	int baudrate = DEFAULT_BAUD_RATE;
	char *token;
	int ret;
	int c;

	for (;;) {
		c = getopt(argc, argv, "hs:u:");
		if (c < 0)
			break;
		switch (c) {
		case 's':
			host_address = strtok(strdup(optarg), ":");
			token = strtok(NULL, "");
			if (token)
				host_port = atoi(token);
			break;
		case 'u':
			uartdev = strtok(strdup(optarg), "@");
			token = strtok(NULL, "");
			if (token)
				baudrate = atoi(token);
			break;
		default:
		case 'h':
			usage();
			break;
		}
	}

	if (host_address) {
		ret = diag_sock_connect(host_address, host_port);
		if (ret < 0)
			err(1, "failed to connect to client");
	} else if (uartdev) {
		ret = diag_uart_open(uartdev, baudrate);
		if (ret < 0)
			errx(1, "failed to open uart\n");
	}

	diag_usb_open("/dev/ffs-diag");

	ret = diag_unix_open();
	if (ret < 0)
		errx(1, "failed to create unix socket dm\n");

	peripheral_init();

	diag_masks_init();

	register_app_cmds();
	register_common_cmds();

	watch_run();

	return 0;
}
