#ifndef __DIAG_H__
#define __DIAG_H__

#include "list.h"

#define BIT(x) (1 << (x))

#define DIAG_FEATURE_FEATURE_MASK_SUPPORT	BIT(0)
#define DIAG_FEATURE_LOG_ON_DEMAND_APPS		BIT(2)
#define DIAG_FEATURE_REQ_RSP_SUPPORT		BIT(4)
#define DIAG_FEATURE_APPS_HDLC_ENCODE		BIT(6)
#define DIAG_FEATURE_STM			BIT(9)
#define DIAG_FEATURE_PERIPHERAL_BUFFERING	BIT(10)
#define DIAG_FEATURE_MASK_CENTRALIZATION	BIT(11)
#define DIAG_FEATURE_SOCKETS_ENABLED		BIT(13)

#define DIAG_CMD_SUBSYS_DISPATCH       75

struct diag_client {
	const char *name;
	int fd;

	struct list_head outq;
	struct list_head node;
};

struct peripheral {
	struct list_head  node;

	char *name;

	unsigned long features;

	struct list_head cntlq;
	struct list_head dataq;

	int cntl_fd;
	int data_fd;
	int cmd_fd;
};

struct diag_cmd {
	struct list_head node;

	unsigned int first;
	unsigned int last;

	struct peripheral *peripheral;
};

void queue_push(struct list_head *queue, uint8_t *msg, size_t msglen);

extern struct list_head diag_cmds;

int diag_data_recv(int fd, void *data);

#endif
