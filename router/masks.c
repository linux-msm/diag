/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "diag.h"
#include "masks.h"
#include "peripheral.h"
#include "util.h"

#define APPS_BUF_SIZE 16384

struct diag_mask_info {
	void *ptr;
	int mask_len;
	uint8_t status;
}__packed;

static struct diag_mask_info msg_mask;
static struct diag_mask_info msg_bt_mask;
static struct diag_mask_info log_mask;
static struct diag_mask_info event_mask;

uint16_t event_max_num_bits;

static int diag_mask_init(struct diag_mask_info *mask_info, int mask_len,
			    int update_buf_len)
{
	if (!mask_info || mask_len < 0 || update_buf_len < 0)
		return -EINVAL;

	mask_info->status = DIAG_CTRL_MASK_INVALID;
	mask_info->mask_len = mask_len;
	if (mask_len > 0) {
		mask_info->ptr = malloc(mask_len);
		if (!mask_info->ptr)
			return -ENOMEM;
		memset(mask_info->ptr, 0, mask_len);
	}

	return 0;
}

static int diag_create_msg_mask_table_entry(struct diag_msg_mask_t *msg_mask,
				    uint32_t entry)
{
	if (!msg_mask)
		return -EIO;

	if (entry > NUM_OF_MASK_RANGES)
		return -EINVAL;

	msg_mask->ssid_first = ssid_first_arr[entry];
	msg_mask->ssid_last = ssid_last_arr[entry];
	msg_mask->ssid_last_tools = ssid_last_arr[entry];
	msg_mask->range = msg_mask->ssid_last - msg_mask->ssid_first + 1;

	if (msg_mask->range < MAX_SSID_PER_RANGE)
		msg_mask->range = MAX_SSID_PER_RANGE;
	msg_mask->range_tools = msg_mask->range;

	if (msg_mask->range > 0) {
		msg_mask->ptr = malloc(msg_mask->range * sizeof(uint32_t));
		if (!msg_mask->ptr)
			return -ENOMEM;
		memset(msg_mask->ptr, 0xFF, msg_mask->range * sizeof(uint32_t));
	}

	return 0;
}

static int diag_create_build_time_mask()
{
	int err = 0;
	struct diag_msg_mask_t *build_mask = msg_bt_mask.ptr;
	int i;

	for (i = 0; i < MSG_MASK_TBL_CNT; i++, build_mask++) {
		err = diag_create_msg_mask_table_entry(build_mask, i);
		if (err)
			break;
	}

	return err;
}

static int diag_create_msg_mask_table()
{
	int err = 0;
	struct diag_msg_mask_t *mask = msg_mask.ptr;
	int i;

	for (i = 0; i < MSG_MASK_TBL_CNT; i++, mask++) {
		err = diag_create_msg_mask_table_entry(mask, i);
		if (err)
			break;
	}

	return err;
}

static int diag_msg_mask_init()
{
	int ret;

	ret = diag_mask_init(&msg_mask, MSG_MASK_SIZE, APPS_BUF_SIZE);
	if (ret)
		return ret;

	ret = diag_create_msg_mask_table();
	if (ret) {
		printf("diag: Unable to create msg masks, err: %d\n", ret);
		return ret;
	}

	return ret;
}

static void diag_msg_mask_exit()
{
	struct diag_msg_mask_t *mask = msg_mask.ptr;
	int i;

	if (mask) {
		for (i = 0; i < MSG_MASK_TBL_CNT; i++, mask++)
			free(mask->ptr);
		free(msg_mask.ptr);
	}
}

static int diag_build_time_mask_init()
{
	int err;

	/* There is no need for update buffer for Build Time masks */
	err = diag_mask_init(&msg_bt_mask, MSG_MASK_SIZE, 0);
	if (err)
		return err;

	err = diag_create_build_time_mask();
	if (err) {
		printf("diag: Unable to create msg build time masks, err: %d\n", err);
		return err;
	}

	return 0;
}

static void diag_build_time_mask_exit(void)
{
	struct diag_msg_mask_t *mask = msg_bt_mask.ptr;
	int i;

	if (mask) {
		for (i = 0; i < MSG_MASK_TBL_CNT; i++, mask++)
			free(mask->ptr);
		free(msg_bt_mask.ptr);
	}
}

static int diag_create_log_mask_table(void)
{
	struct diag_log_mask_t *mask = log_mask.ptr;
	uint8_t equip_id;
	int err = 0;

	for (equip_id = 0; equip_id < MAX_EQUIP_ID; equip_id++, mask++) {
		mask->equip_id = equip_id;
		mask->num_items = LOG_GET_ITEM_NUM(log_code_last_tbl[equip_id]);
		mask->num_items_tools = mask->num_items;
		if (BITS_TO_BYTES(mask->num_items) > MAX_ITEMS_PER_EQUIP_ID)
			mask->range = BITS_TO_BYTES(mask->num_items);
		else
			mask->range = MAX_ITEMS_PER_EQUIP_ID;
		mask->range_tools = mask->range;
		mask->ptr = malloc(mask->range);
		if (!mask->ptr) {
			err = -ENOMEM;
			break;
		}
		memset(mask->ptr, 0, mask->range);
	}

	return err;
}

static int diag_log_mask_init(void)
{
	int ret;

	ret = diag_mask_init(&log_mask, LOG_MASK_SIZE, APPS_BUF_SIZE);
	if (ret)
		return ret;

	return diag_create_log_mask_table();
}

static void diag_log_mask_exit(void)
{
	struct diag_log_mask_t *mask = log_mask.ptr;
	int i;

	if (mask) {
		for (i = 0; i < MAX_EQUIP_ID; i++, mask++)
			free(mask->ptr);
		free(log_mask.ptr);
	}
}

static int diag_event_mask_init(void)
{
	event_max_num_bits = APPS_EVENT_LAST_ID;

	return diag_mask_init(&event_mask, EVENT_MASK_SIZE, APPS_BUF_SIZE);
}

static void diag_event_mask_exit(void)
{
	free(event_mask.ptr);
}

int diag_masks_init()
{
	if (diag_msg_mask_init() ||
		diag_build_time_mask_init() ||
		diag_log_mask_init() ||
		diag_event_mask_init()) {
		diag_masks_exit();
		printf("diag: Could not initialize diag mask buffers\n");

		return -ENOMEM;
	}

	return 0;
}

void diag_masks_exit()
{
	diag_msg_mask_exit();
	diag_build_time_mask_exit();
	diag_log_mask_exit();
	diag_event_mask_exit();
}

uint8_t diag_get_log_mask_status()
{
	return log_mask.status;
}

void diag_cmd_disable_log()
{
	struct diag_log_mask_t *log_item = log_mask.ptr;
	int i;

	for (i = 0; i < MAX_EQUIP_ID; i++, log_item++) {
		memset(log_item->ptr, 0, log_item->range);
	}
	log_mask.status = DIAG_CTRL_MASK_ALL_DISABLED;
}

void diag_cmd_get_log_range(uint32_t *ranges, uint32_t count)
{
	struct diag_log_mask_t *log_item = log_mask.ptr;
	int i;

	for (i = 0; i < MIN(MAX_EQUIP_ID, count); i++, log_item++) {
		ranges[i] = log_item->num_items_tools;
	}
}

int diag_cmd_set_log_mask(uint8_t equip_id, uint32_t *num_items, uint8_t *mask, uint32_t *mask_size)
{
	struct diag_log_mask_t *log_item = log_mask.ptr;
	void *tmp_buf;
	int i;

	for (i = 0; i < MAX_EQUIP_ID; i++, log_item++) {
		if (log_item->equip_id != equip_id)
			continue;

#if 0
		diag_dbg(DIAG_DBG_MASKS, "Found equip_id=%d\n"
				"current num_items=%u range=%u\n"
				"request num_items=%u range=%u\n",
				log_item->equip_id,
				log_item->num_items_tools, log_item->range_tools,
				*num_items, BITS_TO_BYTES(*num_items));
#endif

		log_item->num_items_tools = MIN(*num_items, MAX_ITEMS_ALLOWED);
		*mask_size = BITS_TO_BYTES(log_item->num_items_tools);
		memset(log_item->ptr, 0, log_item->range_tools);

		if (*mask_size > log_item->range_tools) {
			tmp_buf = realloc(log_item->ptr, *mask_size);
			if (!tmp_buf) {
				log_mask.status = DIAG_CTRL_MASK_INVALID;
				warn("Failed to reallocate log mask\n");

				return -errno;
			}
			log_item->ptr = tmp_buf;
			memset(log_item->ptr, 0, *mask_size);
			log_item->range_tools = *mask_size;
		}
		*num_items = log_item->num_items_tools;
		memcpy(log_item->ptr, mask, *mask_size);
		log_mask.status = DIAG_CTRL_MASK_VALID;

		return 0;
	}

	return 1;
}

int diag_cmd_get_log_mask(uint32_t equip_id, uint32_t *num_items, uint8_t ** mask, uint32_t *mask_size)
{
	struct diag_log_mask_t *log_item = log_mask.ptr;
	int i;

	for (i = 0; i < MAX_EQUIP_ID; i++, log_item++) {
		if (log_item->equip_id != equip_id) {
			continue;
		}

		*num_items = log_item->num_items_tools;
		*mask_size = BITS_TO_BYTES(log_item->num_items_tools);
		*mask = malloc(*mask_size);
		if (!*mask) {
			warn("Failed to allocate log mask\n");

			return -errno;
		}
		memcpy(*mask, log_item->ptr, *mask_size);

		return 0;
	}

	return 1;
}

void diag_cmd_get_ssid_range(uint32_t *count, struct diag_ssid_range_t **ranges)
{
	struct diag_msg_mask_t *msg_item = msg_mask.ptr;
	struct diag_ssid_range_t *range;
	int i;

	*count = MSG_MASK_TBL_CNT;
	*ranges = calloc(*count, sizeof(**ranges));
	if (!*ranges) {
		warn("Failed to allocate ssid ranges\n");

		return;
	}

	range = *ranges;
	for (i = 0; i < *count; i++, msg_item++, range++) {
		range->ssid_first = msg_item->ssid_first;
		range->ssid_last = msg_item->ssid_last_tools;
	}
}

uint8_t diag_get_build_mask_status()
{
	return msg_bt_mask.status;
}

int diag_cmd_get_build_mask(struct diag_ssid_range_t *range, uint32_t **mask)
{
	struct diag_msg_mask_t *msg_item = msg_bt_mask.ptr;
	uint32_t num_entries = 0;
	uint32_t mask_size = 0;
	int i;

	for (i = 0;i < MSG_MASK_TBL_CNT; i++, msg_item++) {
		if (msg_item->ssid_first != range->ssid_first) {
			continue;
		}
		num_entries = range->ssid_last - range->ssid_first + 1;
		if (num_entries > msg_item->range) {
			warn("diag: Truncating ssid range for ssid_first: %d ssid_last %d\n",
				range->ssid_first, range->ssid_last);
			num_entries = msg_item->range;
			range->ssid_last = range->ssid_first + msg_item->range;
		}
		mask_size = num_entries * sizeof(uint32_t);
		*mask = malloc(mask_size);
		if (!*mask) {
			warn("Failed to allocate build mask\n");

			return -errno;
		}
		memcpy(*mask, msg_item->ptr, mask_size);

		return 0;
	}

	return 1;
}

uint8_t diag_get_msg_mask_status()
{
	return msg_mask.status;
}

int diag_cmd_get_msg_mask(struct diag_ssid_range_t *range, uint32_t **mask)
{
	struct diag_msg_mask_t *msg_item = msg_mask.ptr;
	uint32_t mask_size = 0;
	int i;

	for (i = 0; i < MSG_MASK_TBL_CNT; i++, msg_item++) {
		if ((range->ssid_first < msg_item->ssid_first) ||
		    (range->ssid_first > msg_item->ssid_last_tools)) {
			continue;
		}

		mask_size = msg_item->range * sizeof(**mask);
		range->ssid_first = msg_item->ssid_first;
		range->ssid_last = msg_item->ssid_last;
		*mask = malloc(mask_size);
		if (!*mask) {
			warn("Failed to allocate event mask\n");

			return -errno;
		}
		memcpy(*mask, msg_item->ptr, mask_size);

		return 0;
	}

	return 1;
}

int diag_cmd_set_msg_mask(struct diag_ssid_range_t range, const uint32_t *mask)
{
	struct diag_msg_mask_t *msg_item = msg_mask.ptr;
	uint32_t num_msgs = 0;
	struct diag_msg_mask_t *mask_next = NULL;
	uint32_t offset = 0;
	void *tmp_buf;
	int i;

	for (i = 0; i < MSG_MASK_TBL_CNT; i++, msg_item++) {
		if (i < (MSG_MASK_TBL_CNT - 1)) {
			mask_next = msg_item;
			mask_next++;
		} else{
			mask_next = NULL;
		}

		if ((range.ssid_first < msg_item->ssid_first) ||
			(range.ssid_first > msg_item->ssid_first + MAX_SSID_PER_RANGE) ||
			(mask_next && (range.ssid_first >= mask_next->ssid_first))) {
			continue;
		}

		mask_next = NULL;
		num_msgs = range.ssid_last - range.ssid_first + 1;
		if (num_msgs > MAX_SSID_PER_RANGE) {
			warn("diag: Truncating ssid range, %d-%d to max allowed: %d\n",
				msg_item->ssid_first, msg_item->ssid_last,
				MAX_SSID_PER_RANGE);
			num_msgs = MAX_SSID_PER_RANGE;
			msg_item->range_tools = MAX_SSID_PER_RANGE;
			msg_item->ssid_last_tools = msg_item->ssid_first + msg_item->range_tools;
		}
		if (range.ssid_last > msg_item->ssid_last_tools) {
			if (num_msgs != MAX_SSID_PER_RANGE)
				msg_item->ssid_last_tools = range.ssid_last;
			msg_item->range_tools = msg_item->ssid_last_tools - msg_item->ssid_first + 1;
			tmp_buf = realloc(msg_item->ptr, msg_item->range_tools * sizeof(*mask));
			if (!tmp_buf) {
				msg_mask.status = DIAG_CTRL_MASK_INVALID;
				warn("Failed to reallocate msg mask\n");

				return -errno;
			}
			msg_item->ptr = tmp_buf;
		}

		offset = range.ssid_first - msg_item->ssid_first;
		if (offset + num_msgs > msg_item->range_tools) {
			warn("diag: Not in msg mask range, num_msgs: %d, offset: %d\n",
			       num_msgs, offset);

			return 1;
		}
		memcpy(msg_item->ptr + offset, mask, num_msgs * sizeof(*mask));
		msg_mask.status = DIAG_CTRL_MASK_VALID;

		return 0;
	}

	return 1;
}

void diag_cmd_set_all_msg_mask(uint32_t mask)
{
	struct diag_msg_mask_t *msg_item = msg_mask.ptr;
	int i;

	msg_mask.status = mask ? DIAG_CTRL_MASK_ALL_ENABLED :
						DIAG_CTRL_MASK_ALL_DISABLED;
	for (i = 0; i < MSG_MASK_TBL_CNT; i++, msg_item++) {
		memset(msg_item->ptr , mask , msg_item->range_tools * sizeof(mask));
	}
}

uint8_t diag_get_event_mask_status()
{
	return event_mask.status;
}

int diag_cmd_get_event_mask(uint16_t num_bits, uint8_t **mask)
{
	uint32_t mask_size = BITS_TO_BYTES(num_bits);

	if (num_bits > event_max_num_bits) {
		return 1;
	}

	*mask = malloc(mask_size);
	if (!*mask) {
		warn("Failed to allocate event mask\n");

		return -errno;
	}
	memcpy(*mask, event_mask.ptr, mask_size);

	return 0;
}

int diag_cmd_update_event_mask(uint16_t num_bits, const uint8_t *mask)
{
	void *tmp_buf;

	if (num_bits > event_max_num_bits ) {
		tmp_buf = realloc(event_mask.ptr, BITS_TO_BYTES(num_bits));
		if (!tmp_buf) {
			event_mask.status = DIAG_CTRL_MASK_INVALID;
			warn("Failed to reallocate event mask\n");

			return -errno;
		}

		event_mask.ptr = tmp_buf;
		event_max_num_bits = num_bits;
		event_mask.mask_len = BITS_TO_BYTES(num_bits);
	}
	memcpy(event_mask.ptr, mask, BITS_TO_BYTES(num_bits));
	event_mask.status = DIAG_CTRL_MASK_VALID;

	return 0;
}

void diag_cmd_toggle_events(bool enabled)
{
	if (enabled) {
		memset(event_mask.ptr, 0xff, event_mask.mask_len);
		event_mask.status = DIAG_CTRL_MASK_ALL_ENABLED;
	} else {
		memset(event_mask.ptr, 0x00, event_mask.mask_len);
		event_mask.status = DIAG_CTRL_MASK_ALL_DISABLED;
	}
}
