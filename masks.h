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
#ifndef MASKS_H_
#define MASKS_H_

#include "diag.h"
#include "util.h"

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#define MSG_MASKS_TYPE		0x00000001
#define LOG_MASKS_TYPE		0x00000002
#define EVENT_MASKS_TYPE	0x00000004

#define MSG_MASK_TBL_CNT		25
#define APPS_EVENT_LAST_ID		0x200

#define MAX_SSID_PER_RANGE	200

extern uint16_t event_max_num_bits;

/* LOG CODES */
static const uint32_t log_code_last_tbl[] = {
	0x0,	/* EQUIP ID 0 */
	0x1A02,	/* EQUIP ID 1 */
	0x0,	/* EQUIP ID 2 */
	0x0,	/* EQUIP ID 3 */
	0x4910,	/* EQUIP ID 4 */
	0x5420,	/* EQUIP ID 5 */
	0x0,	/* EQUIP ID 6 */
	0x74FF,	/* EQUIP ID 7 */
	0x0,	/* EQUIP ID 8 */
	0x0,	/* EQUIP ID 9 */
	0xA38A,	/* EQUIP ID 10 */
	0xB201,	/* EQUIP ID 11 */
	0x0,	/* EQUIP ID 12 */
	0xD1FF,	/* EQUIP ID 13 */
	0x0,	/* EQUIP ID 14 */
	0x0,	/* EQUIP ID 15 */
};

#define NUM_OF_MASK_RANGES 25
static const uint32_t ssid_first_arr[NUM_OF_MASK_RANGES] =
	{     0,   500,  1000,  2000,  3000,  4000,  4500,  4600,
	   5000,  5500,  6000,  6500,  7000,  7100,  7200,  8000,
	   8500,  9000,  9500, 10200, 10251, 10300, 10350, 10400,
	   0xC000 };

static const uint32_t ssid_last_arr[NUM_OF_MASK_RANGES] =
	{   120,   506,  1007,  2008,  3014,  4010,  4573,  4615,
	   5033,  5516,  6081,  6521,  7003,  7111,  7201,  8000,
	   8529,  9008,  9510, 10210, 10255, 10300, 10377, 10416,
	   0xC063 };

#define LOG_GET_ITEM_NUM(xx_code)	(xx_code & 0x0FFF)
#define LOG_GET_EQUIP_ID(xx_code)	((xx_code & 0xF000) >> 12)
#define LOG_ITEMS_TO_SIZE(num_items)	((num_items+7)/8)
#define LOG_SIZE_TO_ITEMS(size)		((8*size) - 7)
#define MSG_RANGE_TO_SIZE(range)	(((range).ssid_last - (range).ssid_first +1) * 4)
#define EVENT_COUNT_TO_BYTES(count)	((count/8))

#define MAX_EQUIP_ID	16
#define EVENT_MASK_SIZE 513
#define MAX_ITEMS_PER_EQUIP_ID	512
#define MAX_ITEMS_ALLOWED	0xFFF

#define DIAG_CTRL_MASK_INVALID		0
#define DIAG_CTRL_MASK_ALL_DISABLED	1
#define DIAG_CTRL_MASK_ALL_ENABLED	2
#define DIAG_CTRL_MASK_VALID		3

struct diag_log_mask_t {
	uint8_t equip_id;
	uint32_t num_items;
	uint32_t num_items_tools;
	uint32_t range;
	uint32_t range_tools;
	uint8_t *ptr;
}__packed;

struct diag_ssid_range_t {
	uint16_t ssid_first;
	uint16_t ssid_last;
}__packed;

struct diag_msg_mask_t {
	uint32_t ssid_first;
	uint32_t ssid_last;
	uint32_t ssid_last_tools;
	uint32_t range;
	uint32_t range_tools;
	uint32_t *ptr;
}__packed;

#define MSG_MASK_SIZE	(MSG_MASK_TBL_CNT * sizeof(struct diag_msg_mask_t))
#define LOG_MASK_SIZE	(MAX_EQUIP_ID * sizeof(struct diag_log_mask_t))

int diag_masks_init(void);
void diag_masks_exit(void);

uint8_t diag_get_log_mask_status();
void diag_cmd_disable_log();
void diag_cmd_get_log_range(uint32_t *ranges, uint32_t count);
int diag_cmd_set_log_mask(uint8_t equip_id, uint32_t *num_items, uint8_t *mask, uint32_t *mask_size);
int diag_cmd_get_log_mask(uint32_t equip_id, uint32_t *num_items, uint8_t ** mask, uint32_t *mask_size);

uint8_t diag_get_build_mask_status();
void diag_cmd_get_ssid_range(uint32_t *count, struct diag_ssid_range_t **ranges);
int diag_cmd_get_build_mask(struct diag_ssid_range_t *range, uint32_t **mask);

uint8_t diag_get_msg_mask_status();
int diag_cmd_get_msg_mask(struct diag_ssid_range_t *range, uint32_t **mask);
int diag_cmd_set_msg_mask(struct diag_ssid_range_t range, uint32_t *mask);
void diag_cmd_set_all_msg_mask(uint32_t mask);

uint8_t diag_get_event_mask_status();
int diag_cmd_get_event_mask(uint16_t num_bits, uint8_t **mask);
int diag_cmd_update_event_mask(uint16_t num_bits, uint8_t *mask);
void diag_cmd_toggle_events(uint8_t operation);

#endif /* MASKS_H_ */
