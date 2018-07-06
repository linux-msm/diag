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
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "diag.h"
#include "diag_cntl.h"
#include "dm.h"
#include "hdlc.h"
#include "list.h"
#include "peripheral.h"
#include "peripheral-qrtr.h"
#include "peripheral-rpmsg.h"
#include "util.h"
#include "watch.h"

struct list_head peripherals = LIST_INIT(peripherals);

int peripheral_send(struct peripheral *peripheral, const void *ptr, size_t len)
{
	return peripheral->send(peripheral, ptr, len);
}

void peripheral_close(struct peripheral *peripheral)
{
	peripheral->close(peripheral);
}

int peripheral_init(void)
{
	peripheral_rpmsg_init();
	peripheral_qrtr_init();

	return 0;
}

void peripheral_broadcast_event_mask(void)
{
	struct peripheral *peripheral;
	struct list_head *item;

	list_for_each(item, &peripherals) {
		peripheral = container_of(item, struct peripheral, node);

		diag_cntl_send_event_mask(peripheral);
	}
}

void peripheral_broadcast_log_mask(unsigned int equip_id)
{
	struct peripheral *peripheral;
	struct list_head *item;

	list_for_each(item, &peripherals) {
		peripheral = container_of(item, struct peripheral, node);

		diag_cntl_send_log_mask(peripheral, equip_id);
	}
}

void peripheral_broadcast_msg_mask(struct diag_ssid_range_t *range)
{
	struct peripheral *peripheral;
	struct list_head *item;

	list_for_each(item, &peripherals) {
		peripheral = container_of(item, struct peripheral, node);

		diag_cntl_send_msg_mask(peripheral, range);
	}
}
