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
#include "dm.h"
#include "hdlc.h"
#include "util.h"

#define DIAG_CMD_KEEP_ALIVE_SUBSYS	50
#define DIAG_CMD_KEEP_ALIVE_CMD		3

#define DIAG_CMD_DIAG_VERSION_ID	28
#define DIAG_PROTOCOL_VERSION_NUMBER	2

#define DIAG_CMD_EXTENDED_BUILD_ID	124
#define DIAG_CMD_DIAG_VERSION_NO 0
#define MOBILE_MODEL_NUMBER		0
#define MOBILE_SOFTWARE_REVISION	"OE"
#define MOBILE_MODEL_STRING		"DB410C"
#define MSM_REVISION_NUMBER		2

static int handle_diag_version(struct diag_client *client, const void *buf,
			       size_t len)
{
	uint8_t resp[] = { DIAG_CMD_DIAG_VERSION_ID, DIAG_PROTOCOL_VERSION_NUMBER };

	return dm_send(client, resp, sizeof(resp));
}

static int handle_diag_version_no(struct diag_client *client, const void *buf,
			       size_t len)
{
	uint8_t resp[55];

	memset(resp, 0, 55);

	return dm_send(client, resp, sizeof(resp));
}

static int handle_extended_build_id(struct diag_client *client,
				    const void *buf, size_t len)
{
	struct {
		uint8_t cmd_code;
		uint8_t ver;
		uint16_t reserved;
		uint32_t msm_rev;
		uint32_t mobile_model_number;
		char strings[];
	} __packed *resp;
	size_t resp_size;
	size_t string1_size = strlen(MOBILE_SOFTWARE_REVISION) + 1;
	size_t string2_size = strlen(MOBILE_MODEL_STRING) + 1;
	size_t strings_size = string1_size + string2_size;

	if (len != sizeof(uint8_t))
		return -EMSGSIZE;

	resp_size = sizeof(*resp) + strings_size;

	resp = alloca(resp_size);
	memset(resp, 0, resp_size);

	resp->cmd_code = DIAG_CMD_EXTENDED_BUILD_ID;
	resp->ver = DIAG_PROTOCOL_VERSION_NUMBER;
	resp->msm_rev = MSM_REVISION_NUMBER;
	resp->mobile_model_number = MOBILE_MODEL_NUMBER;
	strcpy(resp->strings, MOBILE_SOFTWARE_REVISION);
	strcpy(resp->strings + string1_size, MOBILE_MODEL_STRING);

	return dm_send(client, resp, resp_size);
}

static int handle_keep_alive(struct diag_client *client, const void *buf,
			     size_t len)
{
	uint8_t resp[16];

	resp[0] = DIAG_CMD_SUBSYS_DISPATCH;
	resp[1] = DIAG_CMD_KEEP_ALIVE_SUBSYS;
	resp[2] = DIAG_CMD_KEEP_ALIVE_CMD;
	memset(resp + 3, 0, sizeof(resp) - 3);

	return dm_send(client, resp, sizeof(resp));
}

void register_app_cmds(void)
{
	register_fallback_cmd(DIAG_CMD_DIAG_VERSION_ID, handle_diag_version);
	register_fallback_cmd(DIAG_CMD_DIAG_VERSION_NO, handle_diag_version_no);
	register_fallback_cmd(DIAG_CMD_EXTENDED_BUILD_ID, handle_extended_build_id);
	register_fallback_subsys_cmd(DIAG_CMD_KEEP_ALIVE_SUBSYS,
				     DIAG_CMD_KEEP_ALIVE_CMD, handle_keep_alive);
}
