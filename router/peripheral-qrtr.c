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
#include <libqrtr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "diag.h"
#include "diag_cntl.h"
#include "dm.h"
#include "peripheral-qrtr.h"
#include "watch.h"
#include "util.h"

#define DIAG_SERVICE_ID		4097

#define DIAG_INSTANCE_BASE_MODEM	0
#define DIAG_INSTANCE_BASE_LPASS	64
#define DIAG_INSTANCE_BASE_WCNSS	128
#define DIAG_INSTANCE_BASE_SENSORS	192
#define DIAG_INSTANCE_BASE_CDSP		256
#define DIAG_INSTANCE_BASE_WDSP		320

enum {
	DIAG_INSTANCE_CNTL,
	DIAG_INSTANCE_CMD,
	DIAG_INSTANCE_DATA,
	DIAG_INSTANCE_DCI_CMD,
	DIAG_INSTANCE_DCI,
};

static int qrtr_cntl_recv(int fd, void *data)
{
	struct peripheral *perif = data;
	struct sockaddr_qrtr sq;
	struct qrtr_packet pkt;
        socklen_t sl;
	uint8_t buf[4096];
	ssize_t n;
	int ret;

	sl = sizeof(sq);
	n = recvfrom(fd, buf, sizeof(buf), 0, (void *)&sq, &sl);
	if (n < 0) {
		ret = -errno;
		if (ret != -ENETRESET)
			fprintf(stderr, "[DIAG-QRTR] recvfrom failed: %d\n", ret);
		return ret;
	}

	ret = qrtr_decode(&pkt, buf, n, &sq);
	if (ret < 0) {
		fprintf(stderr, "[PD-MAPPER] unable to decode qrtr packet\n");
		return ret;
	}

	switch (pkt.type) {
	case QRTR_TYPE_DEL_CLIENT:
		break;
	case QRTR_TYPE_DATA:
		if (!perif->cntl_open) {
			connect(perif->cntl_fd, (struct sockaddr *)&sq, sizeof(sq));
			perif->cntl_open = true;
			watch_add_writeq(perif->cntl_fd, &perif->cntlq);
		}

		return diag_cntl_recv(perif, pkt.data, pkt.data_len);
	case QRTR_TYPE_BYE:
		watch_remove_writeq(perif->cntl_fd);
		perif->cntl_open = false;
		break;
	default:
		fprintf(stderr, "Unhandled DIAG CNTL message from %d:%d (%d)\n",
			pkt.node, pkt.port, pkt.type);
		break;
	}

	return 0;
}

struct non_hdlc_pkt {
	uint8_t start;
	uint8_t version;
	uint16_t length;
	char payload[];
};

static int qrtr_cmd_recv(int fd, void *data)
{
	struct peripheral *perif = data;
	struct non_hdlc_pkt *frame;
	struct sockaddr_qrtr cmdsq;
	struct sockaddr_qrtr sq;
	struct qrtr_packet pkt;
	struct circ_buf *buf = &perif->recv_buf;
        socklen_t sl;
	ssize_t n;
	int ret;

	sl = sizeof(sq);
	n = recvfrom(fd, buf->buf, sizeof(buf->buf), 0, (void *)&sq, &sl);
	if (n < 0) {
		ret = -errno;
		if (ret != -ENETRESET)
			fprintf(stderr, "[DIAG-QRTR] recvfrom failed: %d\n", ret);
		return ret;
	}

	ret = qrtr_decode(&pkt, buf->buf, n, &sq);
	if (ret < 0) {
		fprintf(stderr, "[PD-MAPPER] unable to decode qrtr packet\n");
		return ret;
	}

	switch (pkt.type) {
	case QRTR_TYPE_DEL_CLIENT:
		break;
	case QRTR_TYPE_DATA:
		frame = pkt.data;
		if (frame->start != 0x7e || frame->version != 1) {
			fprintf(stderr, "invalid non-HDLC frame\n");
			break;
		}

		if (sizeof(*frame) + frame->length + 1 > pkt.data_len) {
			fprintf(stderr, "truncated non-HDLC frame\n");
			break;
		}

		if (frame->payload[frame->length] != 0x7e) {
			fprintf(stderr, "non-HDLC frame is not truncated\n");
			break;
		}

		dm_broadcast(frame->payload, frame->length, NULL);
		break;
	case QRTR_TYPE_NEW_SERVER:
		if (pkt.node == 0 && pkt.port == 0)
			break;

		printf("Connecting CMD socket to %d:%d\n", pkt.node, pkt.port);
		cmdsq.sq_family = AF_QIPCRTR;
		cmdsq.sq_node = pkt.node;
		cmdsq.sq_port = pkt.port;

		ret = connect(perif->cmd_fd, (struct sockaddr *)&cmdsq, sizeof(cmdsq));
		if (ret < 0)
			err(1, "failed to connect to %d:%d", cmdsq.sq_node, cmdsq.sq_port);
		watch_add_writeq(perif->cmd_fd, &perif->cmdq);
		break;
	case QRTR_TYPE_DEL_SERVER:
		watch_remove_writeq(perif->cmd_fd);
		break;
	default:
		fprintf(stderr, "Unhandled DIAG CMD message from %d:%d (%d)\n",
			pkt.node, pkt.port, pkt.type);
		break;
	}

	return 0;
}

static int qrtr_data_recv(int fd, void *data)
{
	struct peripheral *perif = data;
	struct sockaddr_qrtr sq;
	struct qrtr_packet pkt;
        socklen_t sl;
	uint8_t buf[4096];
	ssize_t n;
	int ret;
	struct non_hdlc_pkt *frame;

	sl = sizeof(sq);
	n = recvfrom(fd, buf, sizeof(buf), 0, (void *)&sq, &sl);
	if (n < 0) {
		ret = -errno;
		if (ret != -ENETRESET)
			fprintf(stderr, "[DIAG-QRTR] recvfrom failed: %d\n", ret);
		return ret;
	}

	ret = qrtr_decode(&pkt, buf, n, &sq);
	if (ret < 0) {
		fprintf(stderr, "[PD-MAPPER] unable to decode qrtr packet\n");
		return ret;
	}

	switch (pkt.type) {
	case QRTR_TYPE_DEL_CLIENT:
		break;
	case QRTR_TYPE_DATA:
		if (!perif->data_open) {
			connect(perif->data_fd, (struct sockaddr *)&sq, sizeof(sq));
			perif->data_open = true;
			watch_add_writeq(perif->data_fd, &perif->dataq);
		}
		frame = pkt.data;
		if (frame->start != 0x7e || frame->version != 1) {
			fprintf(stderr, "invalid non-HDLC frame\n");
			break;
		}

		if (sizeof(*frame) + frame->length + 1 > pkt.data_len) {
			fprintf(stderr, "truncated non-HDLC frame\n");
			break;
		}

		if (frame->payload[frame->length] != 0x7e) {
			fprintf(stderr, "non-HDLC frame is not truncated\n");
			break;
		}
		dm_broadcast(frame->payload, frame->length, perif->flow);
		break;
	case QRTR_TYPE_BYE:
		watch_remove_writeq(perif->data_fd);
		perif->data_open = false;
		break;
	default:
		fprintf(stderr, "Unhandled DIAG DATA message from %d:%d (%d)\n",
			pkt.node, pkt.port, pkt.type);
		break;
	}

	return 0;
}

int qrtr_perif_send(struct peripheral *perif, const void *ptr, size_t len)
{
	if (perif->features & DIAG_FEATURE_APPS_HDLC_ENCODE)
		queue_push(&perif->cmdq, ptr, len);
	else
		hdlc_enqueue(&perif->cmdq, ptr, len);

	return 0;
}

void qrtr_perif_close(struct peripheral *perif)
{
}

static int qrtr_perif_init_subsystem(const char *name, int instance_base)
{
	struct peripheral *perif;
	struct watch_flow *flow;

	perif = calloc(1, sizeof(*perif));

	flow = watch_flow_new();

	perif->name = strdup(name);
	perif->send = qrtr_perif_send;
	perif->close = qrtr_perif_close;
	perif->sockets = true;
	perif->flow = flow;

	list_init(&perif->cmdq);
	list_init(&perif->cntlq);
	list_init(&perif->dataq);

	perif->cntl_fd = qrtr_open(0);
	if (perif->cntl_fd < 0)
		err(1, "failed to create control socket");

	perif->data_fd = qrtr_open(0);
	if (perif->data_fd < 0)
		err(1, "failed to create data socket");

	perif->cmd_fd = qrtr_open(0);
	if (perif->cmd_fd < 0)
		err(1, "failed to create command socket");

	perif->dci_cmd_fd = qrtr_open(0);
	if (perif->dci_cmd_fd < 0)
		err(1, "failed to create dci command socket");

	/*
	 * DIAG does not use the normal packing of "instance << 8 | version" in
	 * the one 32-bit "instance" field of the service notifications, so
	 * pass the DIAG instance information as "version" into these functions
	 * instead.
	 */
	qrtr_publish(perif->cntl_fd, DIAG_SERVICE_ID, instance_base + DIAG_INSTANCE_CNTL, 0);
	qrtr_new_lookup(perif->cmd_fd, DIAG_SERVICE_ID, instance_base + DIAG_INSTANCE_CMD, 0);
	qrtr_publish(perif->data_fd, DIAG_SERVICE_ID, instance_base + DIAG_INSTANCE_DATA, 0);
	qrtr_publish(perif->dci_cmd_fd, DIAG_SERVICE_ID, instance_base + DIAG_INSTANCE_DCI, 0);

	watch_add_readfd(perif->cntl_fd, qrtr_cntl_recv, perif, NULL);
	watch_add_readfd(perif->cmd_fd, qrtr_cmd_recv, perif, NULL);
	watch_add_readfd(perif->data_fd, qrtr_data_recv, perif, flow);
	list_add(&peripherals, &perif->node);
	return 0;
}

int peripheral_qrtr_init(void)
{
	qrtr_perif_init_subsystem("modem", DIAG_INSTANCE_BASE_MODEM);
	qrtr_perif_init_subsystem("lpass", DIAG_INSTANCE_BASE_LPASS);
	qrtr_perif_init_subsystem("wcnss", DIAG_INSTANCE_BASE_WCNSS);
	qrtr_perif_init_subsystem("sensors", DIAG_INSTANCE_BASE_SENSORS);
	qrtr_perif_init_subsystem("cdsp", DIAG_INSTANCE_BASE_CDSP);
	qrtr_perif_init_subsystem("wdsp", DIAG_INSTANCE_BASE_WDSP);

	return 0;
}
