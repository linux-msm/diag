/*
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
#ifndef __PERIPHERAL_H__
#define __PERIPHERAL_H__

#define PERIPHERAL_APPS		0
#define PERIPHERAL_MODEM	1
#define PERIPHERAL_LPASS	2
#define PERIPHERAL_WCNSS	3
#define PERIPHERAL_SENSORS	4
#define PERIPHERAL_WDSP		5
#define PERIPHERAL_CDSP		6
#define PERIPHERAL_NPU		7
#define PERIPHERAL_NSP1		8
#define PERIPHERAL_GPDSP0	9
#define PERIPHERAL_GPDSP1	10
#define PERIPHERAL_HELIOS_M55	11
#define PERIPHERAL_SLATE_APPS	12
#define PERIPHERAL_SLATE_ADSP	13
#define PERIPHERAL_TELE_GVM	14
#define PERIPHERAL_FOTA_GVM	15
#define PERIPHERAL_SOCCP	16

#define UPD_PERIPHERAL_WLAN		17
#define UPD_PERIPHERAL_AUDIO	18
#define UPD_PERIPHERAL_SENSORS	19
#define UPD_PERIPHERAL_CHARGER	20
#define UPD_PERIPHERAL_OEM		21
#define UPD_PERIPHERAL_OIS		22

#define NUM_PERIPHERALS			17
#define NUM_UPD_PERIPHERAL		6
#define MAX_UPD_PERIPHERAL		4

#define PD_UNKNOWN				255

struct diag_ssid_range_t;

int peripheral_init(void);
void peripheral_close(struct peripheral *peripheral);

void peripheral_broadcast_event_mask(void);
void peripheral_broadcast_log_mask(unsigned int equip_id);
void peripheral_broadcast_msg_mask(struct diag_ssid_range_t *range);

int peripheral_send(struct peripheral *peripheral, const void *ptr, size_t len);

#endif
