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

#include <stdlib.h>

#include "diag.h"
#include "dm.h"

/**
 * DOC: Diagnostic Monitor
 */

struct list_head diag_clients = LIST_INIT(diag_clients);

/**
 * dm_add() - register new DM
 * @dm:		DM object to register
 */
void dm_add(struct diag_client *dm)
{
	list_add(&diag_clients, &dm->node);
}

/**
 * dm_send() - enqueue message to DM
 * @dm:		dm to be receiving the message
 * @ptr:	pointer to raw message to be sent
 * @len:	length of message
 */
ssize_t dm_send(struct diag_client *dm, const void *ptr, size_t len)
{
	hdlc_enqueue(&dm->outq, ptr, len);

	return 0;
}

/**
 * dm_broadcast() - send message to all registered DMs
 * @ptr:	pointer to raw message to be sent
 * @len:	length of message
 */
void dm_broadcast(const void *ptr, size_t len)
{
	struct diag_client *dm;
	struct list_head *item;

	list_for_each(item, &diag_clients) {
		dm = container_of(item, struct diag_client, node);

		hdlc_enqueue(&dm->outq, ptr, len);
	}
}

