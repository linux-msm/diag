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
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "hdlc.h"
#include "masks.h"
#include "peripheral.h"
#include "util.h"

struct diag_log_cmd_mask {
	uint32_t equip_id;
	uint32_t num_items;
	uint8_t mask[0];
}__packed;

#define DIAG_CMD_STATUS_SUCCESS					0
#define DIAG_CMD_STATUS_INVALID_EQUIPMENT_ID	1

#define DIAG_CMD_LOGGING_CONFIGURATION 	0x73
#define DIAG_CMD_OP_LOG_DISABLE		0
#define DIAG_CMD_OP_GET_LOG_RANGE	1
#define DIAG_CMD_OP_SET_LOG_MASK	3
#define DIAG_CMD_OP_GET_LOG_MASK	4

#define DIAG_CMD_EXTENDED_MESSAGE_CONFIGURATION 	0x7d
#define DIAG_CMD_OP_GET_SSID_RANGE	1
#define DIAG_CMD_OP_GET_BUILD_MASK	2
#define DIAG_CMD_OP_GET_MSG_MASK	3
#define DIAG_CMD_OP_SET_MSG_MASK	4
#define DIAG_CMD_OP_SET_ALL_MSG_MASK	5

#define DIAG_CMD_MSG_STATUS_UNSUCCESSFUL		0
#define DIAG_CMD_MSG_STATUS_SUCCESSFUL			1

static int handle_logging_configuration(struct diag_client *client,
					const void *buf, size_t len)
{
	const struct diag_log_cmd_header {
		uint8_t cmd_code;
		uint8_t reserved[3];
		uint32_t operation;
	}__packed *request_header = buf;

	switch (request_header->operation) {
	case DIAG_CMD_OP_LOG_DISABLE: {
		struct {
			struct diag_log_cmd_header header;
			uint32_t status;
		} __packed resp;

		if (sizeof(*request_header) != len)
			return -EMSGSIZE;

		memcpy(&resp, request_header, sizeof(*request_header));
		diag_cmd_disable_log();
		resp.status = DIAG_CMD_STATUS_SUCCESS;

		peripheral_broadcast_log_mask(0);

		hdlc_enqueue(&client->outq, &resp, sizeof(resp));
		break;
	}
	case DIAG_CMD_OP_GET_LOG_RANGE: {
		struct {
			struct diag_log_cmd_header header;
			uint32_t status;
			uint32_t ranges[MAX_EQUIP_ID];
		} __packed resp;

		if (sizeof(*request_header) != len)
			return -EMSGSIZE;

		memcpy(&resp, request_header, sizeof(*request_header));
		diag_cmd_get_log_range(resp.ranges, MAX_EQUIP_ID);
		resp.status = DIAG_CMD_STATUS_SUCCESS;

		hdlc_enqueue(&client->outq, &resp, sizeof(resp));
		break;
	}
	case DIAG_CMD_OP_SET_LOG_MASK: {
		struct diag_log_cmd_mask *mask_to_set = (struct diag_log_cmd_mask*)(buf + sizeof(struct diag_log_cmd_header));
		struct {
			struct diag_log_cmd_header header;
			uint32_t status;
			struct diag_log_cmd_mask mask_structure;
		} __packed *resp;
		uint32_t resp_size = sizeof(*resp);
		uint32_t mask_size = sizeof(*mask_to_set) + LOG_ITEMS_TO_SIZE(mask_to_set->num_items);

		if (sizeof(*request_header) + mask_size != len)
			return -EMSGSIZE;

		resp_size += mask_size;
		resp = malloc(resp_size);
		if (!resp) {
			warn("Failed to allocate response packet\n");
			return -errno;
		}
		memcpy(resp, request_header, sizeof(*request_header));
		diag_cmd_set_log_mask(mask_to_set->equip_id, &mask_to_set->num_items, mask_to_set->mask, &mask_size);
		memcpy(&resp->mask_structure, mask_to_set, mask_size); // num_items might have been capped!!!
		resp->status = DIAG_CMD_STATUS_SUCCESS;

		peripheral_broadcast_log_mask(resp->mask_structure.equip_id);

		hdlc_enqueue(&client->outq, resp, resp_size);
		free(resp);

		break;
	}
	case DIAG_CMD_OP_GET_LOG_MASK: {
		uint32_t *equip_id = (uint32_t *)(buf + sizeof(struct diag_log_cmd_header));
		struct get_log_response_resp {
			struct diag_log_cmd_header header;
			uint32_t status;
			struct diag_log_cmd_mask mask_structure;
		} __packed *resp;
		uint32_t num_items = 0;
		uint8_t *mask;
		uint32_t mask_size = 0;
		uint32_t resp_size = sizeof(*resp);

		if (sizeof(*request_header) + sizeof(*equip_id) != len)
			return -EMSGSIZE;

		if (diag_cmd_get_log_mask(*equip_id, &num_items, &mask, &mask_size) == 0) {
			resp_size += mask_size;
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			memcpy(resp, request_header, sizeof(*request_header));
			resp->mask_structure.equip_id = *equip_id;
			resp->mask_structure.num_items = num_items;
			if (mask != NULL) {
				memcpy(&resp->mask_structure.mask, mask, mask_size);
				free(mask);
			}
			resp->status = DIAG_CMD_STATUS_SUCCESS;
		} else {
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			memcpy(resp, request_header, sizeof(*request_header));
			resp->mask_structure.equip_id = *equip_id;
			resp->mask_structure.num_items = num_items;
			resp->status = DIAG_CMD_STATUS_INVALID_EQUIPMENT_ID;
		}

		peripheral_broadcast_log_mask(resp->mask_structure.equip_id);

		hdlc_enqueue(&client->outq, resp, resp_size);
		free(resp);

		break;
	}
	default:
		warn("Unrecognized operation %d!!!", request_header->operation);
		return -EINVAL;
	}

	return 0;
}

static int handle_extended_message_configuration(struct diag_client *client,
						 const void *buf, size_t len)
{
	const struct diag_msg_cmd_header {
		uint8_t cmd_code;
		uint8_t operation;
	}__packed *request_header = buf;

	switch (request_header->operation) {
	case DIAG_CMD_OP_GET_SSID_RANGE: {
		struct {
			struct diag_msg_cmd_header header;
			uint8_t status;
			uint8_t reserved;
			uint32_t range_cnt;
			struct diag_ssid_range_t ranges[];
		} __packed *resp;
		uint32_t resp_size = sizeof(*resp);
		uint32_t count = 0;
		struct diag_ssid_range_t *ranges = NULL;
		uint32_t ranges_size = 0;

		if (sizeof(*request_header) != len)
			return -EMSGSIZE;

		diag_cmd_get_ssid_range(&count, &ranges);
		ranges_size = count * sizeof(*ranges);
		resp_size += ranges_size;
		resp = malloc(resp_size);
		if (!resp) {
			warn("Failed to allocate response packet\n");
			return -errno;
		}
		memcpy(resp, request_header, sizeof(*request_header));
		resp->range_cnt = count;
		if (ranges != NULL) {
			memcpy(resp->ranges, ranges, ranges_size);
			free(ranges);
		}
		resp->status = DIAG_CMD_MSG_STATUS_SUCCESSFUL;

		hdlc_enqueue(&client->outq, resp, resp_size);
		free(resp);

		break;
	}
	case DIAG_CMD_OP_GET_BUILD_MASK: {
		struct diag_ssid_range_t *range = (struct diag_ssid_range_t *)(buf + sizeof(struct diag_msg_cmd_header));
		struct {
			struct diag_msg_cmd_header header;
			uint8_t status;
			uint8_t reserved;
			uint32_t bld_masks[];
		} __packed *resp;
		uint32_t resp_size = sizeof(*resp);
		uint32_t *masks = NULL;
		uint32_t masks_size = 0;

		if (sizeof(*request_header) + sizeof(*range) != len)
			return -EMSGSIZE;

		if (diag_cmd_get_build_mask(range, &masks) == 0) {
			masks_size = MSG_RANGE_TO_SIZE(*range);
			resp_size += masks_size;
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			memcpy(resp, request_header, sizeof(*request_header));
			if (masks != NULL) {
				memcpy(resp->bld_masks, masks, masks_size);
				free(masks);
			}
			resp->status = DIAG_CMD_MSG_STATUS_SUCCESSFUL;
		} else {
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			memcpy(resp, request_header, sizeof(*request_header));
			resp->status = DIAG_CMD_MSG_STATUS_UNSUCCESSFUL;
		}

		hdlc_enqueue(&client->outq, resp, resp_size);
		free(resp);

		break;
	}
	case DIAG_CMD_OP_GET_MSG_MASK: {
		struct diag_ssid_range_t range;
		struct {
			struct diag_msg_cmd_header header;
			uint8_t status;
			uint8_t rsvd;
			uint32_t rt_masks[];
		} __packed *resp;
		uint32_t resp_size = sizeof(*resp);
		uint32_t *masks = NULL;
		uint32_t masks_size = 0;

		if (sizeof(*request_header) + sizeof(range) != len)
			return -EMSGSIZE;

		memcpy(&range, buf + sizeof(struct diag_msg_cmd_header), sizeof(range));

		if (diag_cmd_get_msg_mask(&range, &masks) == 0) {
			masks_size = MSG_RANGE_TO_SIZE(range);
			resp_size += masks_size;
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			memcpy(resp, request_header, sizeof(*request_header));
			if (masks != NULL) {
				memcpy(resp->rt_masks, masks, masks_size);
				free(masks);
			}
			resp->status = DIAG_CMD_MSG_STATUS_SUCCESSFUL;
		} else {
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			memcpy(resp, request_header, sizeof(*request_header));
			resp->status = DIAG_CMD_MSG_STATUS_UNSUCCESSFUL;
		}

		hdlc_enqueue(&client->outq, resp, resp_size);
		free(resp);

		break;
	}
	case DIAG_CMD_OP_SET_MSG_MASK: {
		const struct {
			struct diag_msg_cmd_header header;
			struct diag_ssid_range_t range;
			uint16_t rsvd;
			uint32_t masks[];
		} __packed *req = buf;
		struct {
			struct diag_msg_cmd_header header;
			struct diag_ssid_range_t range;
			uint8_t status;
			uint8_t rsvd;
			uint32_t rt_masks[0];
		} __packed *resp;
		uint32_t resp_size = sizeof(*resp);
		uint32_t masks_size = MSG_RANGE_TO_SIZE(req->range);

		if (sizeof(*req) + masks_size != len)
			return -EMSGSIZE;

		if (diag_cmd_set_msg_mask(req->range, req->masks) == 0) {
			resp_size += masks_size;
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			resp->header = req->header;
			resp->range = req->range;
			resp->rsvd = req->rsvd;
			if (req->masks != NULL) {
				memcpy(resp->rt_masks, req->masks, masks_size);
			}
			resp->status = DIAG_CMD_MSG_STATUS_SUCCESSFUL;

			peripheral_broadcast_msg_mask(&resp->range);
		} else {
			resp = malloc(resp_size);
			if (!resp) {
				warn("Failed to allocate response packet\n");
				return -errno;
			}
			resp->header = req->header;
			resp->range = req->range;
			resp->rsvd = req->rsvd;
			resp->status = DIAG_CMD_MSG_STATUS_UNSUCCESSFUL;
		}

		hdlc_enqueue(&client->outq, resp, resp_size);
		free(resp);

		break;
	}
	case DIAG_CMD_OP_SET_ALL_MSG_MASK: {
		const struct {
			struct diag_msg_cmd_header header;
			uint8_t rsvd;
			uint32_t mask;
		} __packed *req = buf;
		struct {
			struct diag_msg_cmd_header header;
			uint8_t status;
			uint8_t rsvd;
			uint32_t rt_mask;
		} __packed resp;

		if (sizeof(*req) != len)
			return -EMSGSIZE;

		diag_cmd_set_all_msg_mask(req->mask);
		resp.header = req->header;
		resp.rsvd = req->rsvd;
		resp.rt_mask = req->mask;
		resp.status = DIAG_CMD_MSG_STATUS_SUCCESSFUL;

		peripheral_broadcast_msg_mask(NULL);

		hdlc_enqueue(&client->outq, &resp, sizeof(resp));
		break;
	}
	default:
		warnx("Unknown extended message configuration: %d", request_header->operation);
		return -EINVAL;
	}

	return 0;
}


void register_common_cmds(void)
{
	register_common_cmd(DIAG_CMD_LOGGING_CONFIGURATION, handle_logging_configuration);
	register_common_cmd(DIAG_CMD_EXTENDED_MESSAGE_CONFIGURATION, handle_extended_message_configuration);
}
