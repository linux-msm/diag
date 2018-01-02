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
#include <errno.h>
#include <stdlib.h>

#include "diag.h"
#include "hdlc.h"
#include "util.h"

#define DIAG_CMD_KEEP_ALIVE_SUBSYS	50
#define DIAG_CMD_KEEP_ALIVE_CMD		3

#define DIAG_CMD_DIAG_VERSION_ID	28
#define DIAG_PROTOCOL_VERSION_NUMBER	2

static int handle_diag_version(struct diag_client *client, const void *buf,
			       size_t len)
{
	struct {
		uint8_t cmd_code;
		uint8_t ver;
	} __packed resp;

	if (len != sizeof(uint8_t))
		return -EMSGSIZE;

	resp.cmd_code = *(uint8_t*)buf;
	resp.ver = DIAG_PROTOCOL_VERSION_NUMBER;

	return hdlc_enqueue(&client->outq, &resp, sizeof(resp));
}

static int handle_keep_alive(struct diag_client *client, const void *buf,
			     size_t len)
{
	return hdlc_enqueue(&client->outq, buf, len);
}

void register_app_cmds(void)
{
	register_cmd(DIAG_CMD_DIAG_VERSION_ID, handle_diag_version);
	register_subsys_cmd(DIAG_CMD_KEEP_ALIVE_SUBSYS, DIAG_CMD_KEEP_ALIVE_CMD,
			    handle_keep_alive);
}
