/*
 * Copyright (c) 2008-2016, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016, Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "diag.h"
#include "util.h"

#define __packed __attribute__((packed))

#define DIAG_CTRL_MSG_DTR               2
#define DIAG_CTRL_MSG_DIAGMODE          3
#define DIAG_CTRL_MSG_DIAGDATA          4
#define DIAG_CTRL_MSG_FEATURE           8
#define DIAG_CTRL_MSG_EQUIP_LOG_MASK    9
#define DIAG_CTRL_MSG_EVENT_MASK_V2     10
#define DIAG_CTRL_MSG_F3_MASK_V2        11
#define DIAG_CTRL_MSG_NUM_PRESETS       12
#define DIAG_CTRL_MSG_SET_PRESET_ID     13
#define DIAG_CTRL_MSG_LOG_MASK_WITH_PRESET_ID   14
#define DIAG_CTRL_MSG_EVENT_MASK_WITH_PRESET_ID 15
#define DIAG_CTRL_MSG_F3_MASK_WITH_PRESET_ID    16
#define DIAG_CTRL_MSG_CONFIG_PERIPHERAL_TX_MODE 17
#define DIAG_CTRL_MSG_PERIPHERAL_BUF_DRAIN_IMM  18
#define DIAG_CTRL_MSG_CONFIG_PERIPHERAL_WMQ_VAL 19
#define DIAG_CTRL_MSG_DCI_CONNECTION_STATUS     20
#define DIAG_CTRL_MSG_LAST_EVENT_REPORT         22
#define DIAG_CTRL_MSG_LOG_RANGE_REPORT          23
#define DIAG_CTRL_MSG_SSID_RANGE_REPORT         24
#define DIAG_CTRL_MSG_BUILD_MASK_REPORT         25
#define DIAG_CTRL_MSG_DEREG             27
#define DIAG_CTRL_MSG_DCI_HANDSHAKE_PKT         29
#define DIAG_CTRL_MSG_PD_STATUS                 30
#define DIAG_CTRL_MSG_TIME_SYNC_PKT             31

struct diag_cntl_hdr {
	uint32_t cmd;
	uint32_t len;
};

struct cmd_range {
	uint16_t first;
	uint16_t last;
	uint32_t data;
};

#define DIAG_CNTL_CMD_REGISTER	1
struct diag_cntl_cmd_reg {
	struct diag_cntl_hdr hdr;
	uint32_t version;
	uint16_t cmd;
	uint16_t subsys;
	uint16_t count_entries;
	uint16_t port;
	struct cmd_range ranges[];
} __packed;
#define to_cmd_reg(h) container_of(h, struct diag_cntl_cmd_reg, hdr)

#define DIAG_CNTL_CMD_FEATURE_MASK 8
struct diag_cntl_cmd_feature {
	struct diag_cntl_hdr hdr;
	uint32_t mask_len;
	uint8_t mask[];
} __packed;
#define to_cmd_feature(h) container_of(h, struct diag_cntl_cmd_feature, hdr)

#define DIAG_CNTL_CMD_NUM_PRESETS 12
struct diag_cntl_num_presets {
	struct diag_cntl_hdr hdr;
	uint8_t num;
};

static int diag_cntl_register(struct peripheral *peripheral,
			      struct diag_cntl_hdr *hdr, size_t len)
{
	struct diag_cntl_cmd_reg *pkt = to_cmd_reg(hdr);
	struct diag_cmd *dc;
	unsigned int subsys;
	unsigned int cmd;
	unsigned int first;
	unsigned int last;
	int i;

	for (i = 0; i < pkt->count_entries; i++) {
		cmd = pkt->cmd;
		subsys = pkt->subsys;

		if (cmd == 0xff && subsys != 0xff)
			cmd = DIAG_CMD_SUBSYS_DISPATCH;

		first = cmd << 24 | subsys << 16 | pkt->ranges[i].first;
		last = cmd << 24 | subsys << 16 | pkt->ranges[i].last;

		// printf("[%s] register 0x%x - 0x%x\n",
		//	  peripheral->name, first, last);

		dc = malloc(sizeof(*dc));
		if (!dc)
			err(1, "malloc failed");
		memset(dc, 0, sizeof(*dc));

		dc->first = first;
		dc->last = last;
		dc->peripheral = peripheral;

		list_add(&diag_cmds, &dc->node);
	}

	return 0;
}

static int diag_cntl_feature_mask(struct peripheral *peripheral,
				  struct diag_cntl_hdr *hdr, size_t len)
{
	struct diag_cntl_cmd_feature *pkt = to_cmd_feature(hdr);
	uint32_t mask = 0;
	int i;

	for (i = 0; i < pkt->mask_len && i < sizeof(mask); i++)
		mask |= pkt->mask[i] << (8 * i);

	printf("[%s] mask:", peripheral->name);

	if (mask & DIAG_FEATURE_FEATURE_MASK_SUPPORT)
		printf(" FEATURE_MASK_SUPPORT");
	if (mask & DIAG_FEATURE_LOG_ON_DEMAND_APPS)
		printf(" LOG_ON_DEMAND");
	if (mask & DIAG_FEATURE_REQ_RSP_SUPPORT)
		printf(" REQ_RSP");
	if (mask & DIAG_FEATURE_APPS_HDLC_ENCODE)
		printf(" APPS_HDLC_ENCODE");
	if (mask & DIAG_FEATURE_STM)
		printf(" STM");
	if (mask & DIAG_FEATURE_PERIPHERAL_BUFFERING)
		printf(" PERIPHERAL-BUFFERING");
	if (mask & DIAG_FEATURE_MASK_CENTRALIZATION)
		printf(" MASK-CENTERALIZATION");
	if (mask & DIAG_FEATURE_SOCKETS_ENABLED)
		printf(" SOCKETS");

	printf(" (0x%x)\n", mask);

	peripheral->features = mask;

	return 0;
}

void diag_cntl_send_feature_mask(struct peripheral *peripheral)
{
	struct diag_cntl_cmd_feature *pkt;
	size_t len = sizeof(*pkt) + 2;
	uint32_t mask = 0;

	mask = DIAG_FEATURE_FEATURE_MASK_SUPPORT |
	       DIAG_FEATURE_APPS_HDLC_ENCODE;

	pkt = malloc(len);
	pkt->hdr.cmd = DIAG_CNTL_CMD_FEATURE_MASK;
	pkt->hdr.len = len - sizeof(struct diag_cntl_hdr);
	pkt->mask_len = 2;
	pkt->mask[0] = mask >> 8;
	pkt->mask[1] = mask & 0xff;

	queue_push(&peripheral->cntlq, (uint8_t *)pkt, len);
}

int diag_cntl_recv(int fd, void *data)
{
	struct peripheral *peripheral = data;
	struct diag_cntl_hdr *hdr;
	uint8_t buf[4096];
	size_t offset = 0;
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		warn("failed to read from cntl channel");
		return n;
	}

	for (;;) {
		if (offset + sizeof(struct diag_cntl_hdr) > n)
			break;

		hdr = (struct diag_cntl_hdr *)(buf + offset);
		if (offset + sizeof(struct diag_cntl_hdr) + hdr->len > n) {
			warnx("truncated diag cntl command");
			break;
		}

		switch (hdr->cmd) {
		case DIAG_CNTL_CMD_REGISTER:
			diag_cntl_register(peripheral, hdr, n);
			break;
		case DIAG_CNTL_CMD_FEATURE_MASK:
			diag_cntl_feature_mask(peripheral, hdr, n);
			break;
		case DIAG_CNTL_CMD_NUM_PRESETS:
			break;
		default:
			warnx("[%s] unsupported control packet: %d",
			      peripheral->name, hdr->cmd);
			print_hex_dump("CNTL", buf, n);
			break;
		}

		offset += sizeof(struct diag_cntl_hdr) + hdr->len;
	}

	return 0;
}
