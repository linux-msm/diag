/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#define _DEFAULT_SOURCE /* for endian.h */

#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <pthread.h>
#include <signal.h>

#include "diag.h"
#include "dm.h"
#include "mbuf.h"
#include "hdlc.h"
#include "util.h"
#include "watch.h"

#define USB_FFS_EP0_NAME	"ep0"
#define USB_FFS_OUT_NAME	"ep1"
#define USB_FFS_IN_NAME		"ep2"

#define MAX_PACKET_SIZE_FS	64
#define MAX_PACKET_SIZE_HS	512
#define MAX_PACKET_SIZE_SS	1024

#define cpu_to_le16(x)		(x)
#define cpu_to_le32(x)		(x)

#define cpu_to_le16(x)		(x)
#define cpu_to_le32(x)		(x)

#define APPS_BUF_SIZE		16384

struct func_desc {
	struct usb_interface_descriptor intf;
	struct usb_endpoint_descriptor_no_audio source;
	struct usb_endpoint_descriptor_no_audio sink;
} __packed;

struct ss_func_desc {
	struct usb_interface_descriptor intf;
	struct usb_endpoint_descriptor_no_audio source;
	struct usb_ss_ep_comp_descriptor source_comp;
	struct usb_endpoint_descriptor_no_audio sink;
	struct usb_ss_ep_comp_descriptor sink_comp;
} __packed;

struct desc_v2 {
	struct usb_functionfs_descs_head_v2 header;
	// The rest of the structure depends on the flags in the header.
	__le32 fs_count;
	__le32 hs_count;
	__le32 ss_count;
	struct func_desc fs_descs, hs_descs;
	struct ss_func_desc ss_descs;
} __packed;

static const struct func_desc fs_descriptors = {
	.intf = {
		.bLength = sizeof(fs_descriptors.intf),
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
		.bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
		.bInterfaceProtocol = 0x30,
		.iInterface = 1, /* first string from the provided table */
	},
	.source = {
		.bLength = sizeof(fs_descriptors.source),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_OUT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_FS,
	},
	.sink = {
		.bLength = sizeof(fs_descriptors.sink),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 2 | USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_FS,
	},
};

static const struct func_desc hs_descriptors = {
	.intf = {
		.bLength = sizeof(hs_descriptors.intf),
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
		.bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
		.bInterfaceProtocol = 0x30,
		.iInterface = 1, /* first string from the provided table */
	},
	.source = {
		.bLength = sizeof(hs_descriptors.source),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_OUT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_HS,
	},
	.sink = {
		.bLength = sizeof(hs_descriptors.sink),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 2 | USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_HS,
	},
};

static const struct ss_func_desc ss_descriptors = {
	.intf = {
		.bLength = sizeof(ss_descriptors.intf),
		.bDescriptorType = USB_DT_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
		.bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
		.bInterfaceProtocol = 0x30,
		.iInterface = 1, /* first string from the provided table */
	},
	.source = {
		.bLength = sizeof(ss_descriptors.source),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_OUT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_SS,
	},
	.source_comp = {
		.bLength = sizeof(ss_descriptors.source_comp),
		.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
	},
	.sink = {
		.bLength = sizeof(ss_descriptors.sink),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 2 | USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_SS,
	},
	.sink_comp = {
		.bLength = sizeof(ss_descriptors.sink_comp),
		.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
	},
};

#define STR_INTERFACE_ "Diag interface"

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof(STR_INTERFACE_)];
	} __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
	.header = {
		.magic = cpu_to_le32(FUNCTIONFS_STRINGS_MAGIC),
		.length = cpu_to_le32(sizeof(strings)),
		.str_count = cpu_to_le32(1),
		.lang_count = cpu_to_le32(1),
	},
	.lang0 = {
		cpu_to_le16(0x0409), /* en-us */
		STR_INTERFACE_,
	},
};

struct usb_handle {
	int ep0;
	int bulk_out; /* "out" from the host's perspective => source for diagd */
	int bulk_in;  /* "in" from the host's perspective => sink for diagd */

	struct diag_client *dm;
	struct list_head outq;
};

static int ffs_diag_init(const char *ffs_name, struct usb_handle *h)
{
	struct desc_v2 desc;
	int ffs_fd;
	ssize_t n;
	int ret;

	desc.header.magic = cpu_to_le32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
	desc.header.length = cpu_to_le32(sizeof(desc));
	desc.header.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC | FUNCTIONFS_HAS_SS_DESC;
	desc.fs_count = 3;
	desc.hs_count = 3;
	desc.ss_count = 5;
	desc.fs_descs = fs_descriptors;
	desc.hs_descs = hs_descriptors;
	desc.ss_descs = ss_descriptors;

	ret = open(ffs_name, O_DIRECTORY);
	if (ret < 0) {
		warn("cannot open device folder %s", ffs_name);
		goto err_out;
	}
	ffs_fd = ret;

	ret = openat(ffs_fd, USB_FFS_EP0_NAME, O_RDWR);
	if (ret < 0) {
		warn("cannot open control endpoint");
		goto err_close_ffs;
	}
	h->ep0 = ret;

	n = write(h->ep0, &desc, sizeof(desc));
	if (n < 0) {
		warn("failed to write descriptors");
		ret = n;
		goto err_close_ep0;
	}

	n = write(h->ep0, &strings, sizeof(strings));
	if (n < 0) {
		warn("failed to write strings");
		ret = n;
		goto err_close_ep0;
	}

	ret = openat(ffs_fd, USB_FFS_OUT_NAME, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (ret < 0) {
		warn("cannot open bulk-out ep");
		goto err_close_ep0;
	}
	h->bulk_out = ret;

	ret = openat(ffs_fd, USB_FFS_IN_NAME, O_RDWR);
	if (ret < 0) {
		warn("cannot open bulk-in ep");
		goto err_close_bulk_out;
	}
	h->bulk_in = ret;

	close(ffs_fd);

	return 0;

err_close_bulk_out:
	close(h->bulk_out);
err_close_ep0:
	close(h->ep0);
err_close_ffs:
	close(ffs_fd);
err_out:
	return ret;
}

static int diag_ffs_recv(struct mbuf *mbuf, void *data)
{
	struct hdlc_decoder recv_decoder;
	struct circ_buf recv_buf;
	struct usb_handle *ffs = data;
	size_t msglen;
	void *msg;

	memset(&recv_decoder, 0, sizeof(recv_decoder));

	memcpy(recv_buf.buf, mbuf->data, mbuf->offset);
	recv_buf.tail = 0;
	recv_buf.head = mbuf->offset;

	// print_hex_dump("[USB]", mbuf->data, mbuf->offset);

	for (;;) {
		msg = hdlc_decode_one(&recv_decoder, &recv_buf, &msglen);
		if (!msg)
			break;

		// print_hex_dump("  [MSG]", msg, MIN(msglen, 256));

		diag_client_handle_command(ffs->dm, msg, msglen);
	}

	mbuf->offset = 0;
	list_add(&ffs->outq, &mbuf->node);

	return 0;
}

static int ep0_recv(int fd, void *data)
{
	struct usb_functionfs_event event;
	struct usb_handle *ffs = data;
	ssize_t n;

	n = read(fd, &event, sizeof(event));
	if (n <= 0) {
		warn("failed to read ffs ep0");
		return 0;
	}

	switch (event.type) {
	case FUNCTIONFS_ENABLE:
		watch_add_readq(ffs->bulk_out, &ffs->outq, diag_ffs_recv, ffs);
		break;
	}

	return 0;
}

int diag_usb_open(const char *ffs_name)
{
	struct usb_handle *ffs;
	struct mbuf *out_buf;
	int ret;

	ffs = calloc(1, sizeof(struct usb_handle));
	if (!ffs)
		err(1, "couldn't allocate usb_handle");

	out_buf = mbuf_alloc(16384);
	if (!out_buf)
		err(1, "couldn't allocate usb out buffer");

	ret = ffs_diag_init(ffs_name, ffs);
	if (ret < 0) {
		free(ffs);
		return -1;
	}

	list_init(&ffs->outq);
	list_add(&ffs->outq, &out_buf->node);

	watch_add_readfd(ffs->ep0, ep0_recv, ffs);

	ffs->dm = dm_add("USB client", -1, ffs->bulk_in, true);

	return 0;
}
