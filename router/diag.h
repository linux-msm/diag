/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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
#ifndef __DIAG_H__
#define __DIAG_H__

#include <stdint.h>
#include <sys/types.h>

#include "circ_buf.h"
#include "hdlc.h"
#include "list.h"
#include "watch.h"

#define DEFAULT_SOCKET_PORT 2500
#define DEFAULT_BAUD_RATE 115200

#define BIT(x) (1 << (x))

#define DIAG_FEATURE_FEATURE_MASK_SUPPORT			BIT(0)
#define DIAG_FEATURE_DIAG_MASTER_SETS_COMMON_MASK	BIT(1)
#define DIAG_FEATURE_LOG_ON_DEMAND_APPS				BIT(2)
#define DIAG_FEATURE_DIAG_VERSION_RSP_ON_MASTER		BIT(3)
#define DIAG_FEATURE_REQ_RSP_SUPPORT				BIT(4)
#define DIAG_FEATURE_DIAG_PRESET_MASKS				BIT(5)
#define DIAG_FEATURE_APPS_HDLC_ENCODE				BIT(6)
#define DIAG_FEATURE_STM							BIT(9)
#define DIAG_FEATURE_PERIPHERAL_BUFFERING			BIT(10)
#define DIAG_FEATURE_MASK_CENTRALIZATION			BIT(11)
#define DIAG_FEATURE_SOCKETS_ENABLED				BIT(13)
#define DIAG_FEATURE_DCI_EXTENDED_HEADER			BIT(14)
#define DIAG_FEATURE_DIAG_ID					BIT(15)
#define DIAG_FEATURE_PKT_HEADER_UNTAG				BIT(16)

#define DIAG_CMD_SUBSYS_DISPATCH       75
#define DIAG_CMD_SUBSYS_DISPATCH_V2	128

struct diag_client;

struct peripheral {
	struct list_head  node;

	char *name;

	unsigned long features;

	struct list_head cmdq;
	struct list_head cntlq;
	struct list_head dataq;

	int cntl_fd;
	bool cntl_open;
	int data_fd;
	bool data_open;
	int cmd_fd;
	int dci_cmd_fd;

	struct watch_flow *flow;

	int diag_id;

	bool sockets;

	struct circ_buf recv_buf;
	struct hdlc_decoder recv_decoder;

	int (*send)(struct peripheral *perif, const void *ptr, size_t len);
	void (*close)(struct peripheral *perif);
};

extern struct list_head peripherals;

struct diag_cmd {
	struct list_head node;

	unsigned int first;
	unsigned int last;

	struct peripheral *peripheral;
	int(*cb)(struct diag_client *client, const void *buf, size_t len);
};

void queue_push(struct list_head *queue, const void *msg, size_t msglen);
void queue_push_flow(struct list_head *queue, const void *msg, size_t msglen,
		     struct watch_flow *flow);

extern struct list_head diag_cmds;

int diag_sock_connect(const char *hostname, unsigned short port);
int diag_uart_open(const char *uartname, unsigned int baudrate);
int diag_usb_open(const char *ffs_name);
int diag_unix_open(void);

int diag_client_handle_command(struct diag_client *client, uint8_t *data, size_t len);

int hdlc_enqueue(struct list_head *queue, const void *buf, size_t msglen);
int hdlc_enqueue_flow(struct list_head *queue, const void *buf, size_t msglen,
		 struct watch_flow *flow);

void register_fallback_cmd(unsigned int cmd,
			   int(*cb)(struct diag_client *client,
				    const void *buf, size_t len));
void register_fallback_subsys_cmd(unsigned int subsys, unsigned int cmd,
				  int(*cb)(struct diag_client *client,
					   const void *buf, size_t len));
void register_common_cmd(unsigned int cmd, int(*cb)(struct diag_client *client,
						    const void *buf,
						    size_t len));

void register_app_cmds(void);
void register_common_cmds(void);

#endif
