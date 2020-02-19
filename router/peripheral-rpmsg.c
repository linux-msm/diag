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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <netdb.h>
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
#include "util.h"
#include "watch.h"

#define APPS_BUF_SIZE 16384

struct devnode {
	char *devnode;
	char *name;
	char *rproc;

	struct list_head node;
};

struct list_head devnodes = LIST_INIT(devnodes);

struct non_hdlc_pkt {
	uint8_t start;
	uint8_t version;
	uint16_t length;
	char payload[];
};

static int diag_cmd_recv(int fd, void *data)
{
	struct peripheral *peripheral = data;
	struct non_hdlc_pkt *frame;
	uint8_t buf[APPS_BUF_SIZE];
	ssize_t len;

	len = read(fd, buf, sizeof(buf));
	if (len < 0) {
		if (errno != EAGAIN) {
			warn("failed to read from cmd channel");
			peripheral_close(peripheral);
		}
	}

	frame = (struct non_hdlc_pkt *)buf;
	if (frame->start != 0x7e || frame->version != 1) {
		fprintf(stderr, "invalid non-HDLC frame\n");
		return 0;
	}

	if (sizeof(*frame) + frame->length + 1 > len) {
		fprintf(stderr, "truncated non-HDLC frame\n");
		return 0;
	}

	if (frame->payload[frame->length] != 0x7e) {
		fprintf(stderr, "non-HDLC frame is not truncated\n");
		return 0;
	}

	dm_broadcast(frame->payload, frame->length, NULL);

	return 0;
}

static int diag_data_recv_hdlc(int fd, struct peripheral *peripheral)
{
	size_t msglen;
	ssize_t n;
	void *msg;

	for (;;) {
		n = circ_read(fd, &peripheral->recv_buf);
		if (n < 0)
			return -errno;

		for (;;) {
			msg = hdlc_decode_one(&peripheral->recv_decoder,
					      &peripheral->recv_buf,
					      &msglen);
			if (!msg)
				break;

			dm_broadcast(msg, msglen, peripheral->flow);
		}
	}

	/* Not reached */
}

static int diag_data_recv_raw(int fd, struct peripheral *peripheral)
{
	uint8_t buf[4096];
	ssize_t n;

	for (;;) {
		n = read(fd, buf, sizeof(buf));
		if (n < 0)
			return -errno;

		dm_broadcast(buf, n, peripheral->flow);
	}

	/* Not reached */
}

static int diag_data_recv(int fd, void *data)
{
	struct peripheral *peripheral = data;
	ssize_t n;

	if (peripheral->features & DIAG_FEATURE_APPS_HDLC_ENCODE)
		n = diag_data_recv_raw(fd, peripheral);
	else
		n = diag_data_recv_hdlc(fd, peripheral);

	if (n < 0 && n != -EAGAIN) {
		warn("failed to read from data channel");
		peripheral_close(peripheral);
	}

	return 0;
}

static int perif_rpmsg_send(struct peripheral *peripheral, const void *ptr, size_t len)
{
	struct list_head *queue;

	if (peripheral->features & DIAG_FEATURE_REQ_RSP_SUPPORT)
		queue = &peripheral->cmdq;
	else
		queue = &peripheral->dataq;

	if (peripheral->features & DIAG_FEATURE_APPS_HDLC_ENCODE)
		queue_push(queue, ptr, len);
	else
		hdlc_enqueue(queue, ptr, len);

	return 0;
}

static struct devnode *devnode_get(const char *devnode)
{
	struct list_head *item;
	struct devnode *node;

	list_for_each(item, &devnodes) {
		node = container_of(item, struct devnode, node);
		if (strcmp(node->devnode, devnode) == 0)
			return node;
	}

	return NULL;
}

static int devnode_open(const char *rproc, const char *name)
{
	struct list_head *item;
	struct devnode *node;

	list_for_each(item, &devnodes) {
		node = container_of(item, struct devnode, node);
		if (strcmp(node->rproc, rproc) == 0 &&
		    strcmp(node->name, name) == 0)
			return open(node->devnode, O_RDWR);
	}

	return -1;
}

static void devnode_add(const char *devnode, const char *name, const char *rproc)
{
	struct devnode *node;

	node = devnode_get(devnode);
	if (node) {
		warnx("node already in list");
		return;
	}

	node = malloc(sizeof(*node));
	memset(node, 0, sizeof(*node));

	node->devnode = strdup(devnode);
	node->name = strdup(name);
	node->rproc = strdup(rproc);

	list_add(&devnodes, &node->node);
}

static void devnode_remove(const char *devnode)
{
	struct devnode *node;

	node = devnode_get(devnode);
	if (!node)
		return;

	list_del(&node->node);

	free(node->name);
	free(node->devnode);
	free(node->rproc);
}

static const char *peripheral_udev_get_name(struct udev_device *dev)
{
	return udev_device_get_sysattr_value(dev, "name");
}

static const char *peripheral_udev_get_remoteproc(struct udev_device *dev)
{
	struct udev_device *parent;
	const char *p;

	parent = udev_device_get_parent(dev);
	if (!parent)
		return NULL;

	p = udev_device_get_sysattr_value(parent, "rpmsg_name");
	if (p)
		return p;

	return peripheral_udev_get_remoteproc(parent);
}

static int rpmsg_perif_cntl_recv(int fd, void *data)
{
	struct peripheral *peripheral = data;
	uint8_t buf[4096];
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		if (errno != EAGAIN) {
			warn("failed to read from cntl channel");
			peripheral_close(peripheral);
		}
		return 0;
	}

	return diag_cntl_recv(peripheral, buf, n);
}

static void peripheral_open(void *data)
{
	struct peripheral *peripheral = data;
	char *rproc = peripheral->name;
	int ret;
	int fd;

	fd = devnode_open(rproc, "DIAG");
	if (fd < 0)
		fd = devnode_open(rproc, "APPS_RIVA_DATA");
	if (fd < 0) {
		warn("unable to open DIAG channel");
		return;
	}
	peripheral->data_fd = fd;

	fd = devnode_open(rproc, "DIAG_CNTL");
	if (fd < 0)
		fd = devnode_open(rproc, "APPS_RIVA_CTRL");
	if (fd < 0) {
		warn("unable to find DIAG_CNTL channel");
		close(peripheral->data_fd);
		peripheral->data_fd = -1;
		return;
	}
	peripheral->cntl_fd = fd;

	fd = devnode_open(rproc, "DIAG_CMD");
	if (fd >= 0)
		peripheral->cmd_fd = fd;

	ret = fcntl(peripheral->data_fd, F_SETFL, O_NONBLOCK);
	if (ret < 0)
		warn("failed to turn DIAG non blocking");

	watch_add_writeq(peripheral->cntl_fd, &peripheral->cntlq);
	watch_add_writeq(peripheral->data_fd, &peripheral->dataq);
	watch_add_readfd(peripheral->cntl_fd, rpmsg_perif_cntl_recv, peripheral, NULL);
	watch_add_readfd(peripheral->data_fd, diag_data_recv, peripheral, peripheral->flow);
	if (peripheral->cmd_fd >= 0) {
		watch_add_readfd(peripheral->cmd_fd, diag_cmd_recv, peripheral, NULL);
		watch_add_writeq(peripheral->cmd_fd, &peripheral->cmdq);
	}

	/* Send current message mask to the newly found peripheral */
	diag_cntl_send_masks(peripheral);
}

static void perif_rpmsg_close(struct peripheral *peripheral)
{
	diag_cntl_close(peripheral);

	watch_remove_fd(peripheral->data_fd);
	watch_remove_fd(peripheral->cntl_fd);
	watch_remove_fd(peripheral->cmd_fd);

	close(peripheral->data_fd);
	close(peripheral->cntl_fd);
	close(peripheral->cmd_fd);

	list_del(&peripheral->node);
	free(peripheral->name);
	free(peripheral);
}

static int peripheral_create(const char *rproc, const char *channel)
{
	struct peripheral *peripheral;
	struct watch_flow *flow;
	struct list_head *item;

	/* Only trigger the creation of a peripheral on primary channels */
	if (strcmp(channel, "DIAG") && strcmp(channel, "APPS_RIVA_DATA"))
		return 0;

	list_for_each(item, &peripherals) {
		peripheral = container_of(item, struct peripheral, node);
		if (strcmp(peripheral->name, rproc) == 0)
			return 0;
	}

	peripheral = malloc(sizeof(*peripheral));
	memset(peripheral, 0, sizeof(*peripheral));

	flow = watch_flow_new();

	peripheral->name = strdup(rproc);
	peripheral->data_fd = -1;
	peripheral->cntl_fd = -1;
	peripheral->cmd_fd = -1;
	peripheral->send = perif_rpmsg_send;
	peripheral->close = perif_rpmsg_close;
	peripheral->flow = flow;
	list_init(&peripheral->cmdq);
	list_init(&peripheral->cntlq);
	list_init(&peripheral->dataq);
	list_add(&peripherals, &peripheral->node);

	watch_add_timer(peripheral_open, peripheral, 1000, false);

	return 0;
}

static int peripheral_udev_update(int fd, void *data)
{
	struct udev_monitor *mon = data;
	struct udev_device *dev;
	const char *devnode;
	const char *action;
	const char *rproc;
	const char *name;

	dev = udev_monitor_receive_device(mon);
	if (!dev)
		return 0;

	action = udev_device_get_action(dev);
	devnode = udev_device_get_devnode(dev);

	if (!devnode)
		goto unref_dev;

	if (strcmp(action, "add") == 0) {
		name = peripheral_udev_get_name(dev);
		rproc = peripheral_udev_get_remoteproc(dev);

		if (!name || !rproc)
			goto unref_dev;

		devnode_add(devnode, name, rproc);

		peripheral_create(rproc, name);
	} else if (strcmp(action, "remove") == 0) {
		devnode_remove(devnode);
	} else {
		warn("unknown udev action");
	}

unref_dev:
	udev_device_unref(dev);

	return 0;
}

int peripheral_rpmsg_init(void)
{
	struct udev_list_entry *devices;
	struct udev_list_entry *entry;
	struct udev_enumerate *enu;
	struct udev_monitor *mon;
	struct udev_device *dev;
	struct udev *udev;
	const char *devnode;
	const char *path;
	const char *rproc;
	const char *name;
	int fd;

	udev = udev_new();
	if (!udev)
		err(1, "failed to initialize libudev");

	mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "rpmsg", NULL);
	udev_monitor_enable_receiving(mon);

	fd = udev_monitor_get_fd(mon);

	enu = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enu, "rpmsg");
	udev_enumerate_scan_devices(enu);

	devices = udev_enumerate_get_list_entry(enu);
	udev_list_entry_foreach(entry, devices) {
		path = udev_list_entry_get_name(entry);
		dev = udev_device_new_from_syspath(udev, path);

		devnode = udev_device_get_devnode(dev);
		name = peripheral_udev_get_name(dev);
		rproc = peripheral_udev_get_remoteproc(dev);

		if (devnode && name && rproc) {
			devnode_add(devnode, name, rproc);
			peripheral_create(rproc, name);
		}

		udev_device_unref(dev);
	}

	watch_add_readfd(fd, peripheral_udev_update, mon, NULL);

	return 0;
}
