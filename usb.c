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
		.bInterfaceProtocol = 0xff,
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
		.bInterfaceProtocol = 0xff,
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
		.bInterfaceProtocol = 0xff,
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

#define USB_DEVNAME "/dev/ffs-diag"
#define USB_SERIAL 	"0123456789"
#define GADGET_PATH "/sys/kernel/config/usb_gadget/g1"
#define UDC_ATTRIB 	"UDC"
#define UDC_NAME 	"ci_hdrc.0"

struct {
	const char *dev_name;
	char serial[64];
	const char *gadget_path;
	const char *udc_name;
} g_usb_config = { USB_DEVNAME, USB_SERIAL, GADGET_PATH, UDC_NAME };

struct usb_handle {
	int control;
	int bulk_out; /* "out" from the host's perspective => source for diagd */
	int bulk_in;  /* "in" from the host's perspective => sink for diagd */
};

static int init_functionfs(struct usb_handle *h)
{
	int ret = 0;
	ssize_t n;
	struct desc_v2 desc;
	int ffs_fd = -1;

	desc.header.magic = cpu_to_le32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
	desc.header.length = cpu_to_le32(sizeof(desc));
	desc.header.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC | FUNCTIONFS_HAS_SS_DESC;
	desc.fs_count = 3;
	desc.hs_count = 3;
	desc.ss_count = 5;
	desc.fs_descs = fs_descriptors;
	desc.hs_descs = hs_descriptors;
	desc.ss_descs = ss_descriptors;

	ret = open(USB_DEVNAME, O_DIRECTORY);
	if (ret < 0) {
		warn("cannot open device folder %s", USB_DEVNAME);
		goto err;
	}
	ffs_fd = ret;

	ret = openat(ffs_fd, USB_FFS_EP0_NAME, O_RDWR);
	if (ret < 0) {
		warn("cannot open control endpoint");
		goto err;
	}
	h->control = ret;

	n = write(h->control, &desc, sizeof(desc));
	if (n < 0) {
		warn("failed to write descriptors");
		ret = n;
		goto err;
	}

	n = write(h->control, &strings, sizeof(strings));
	if (n < 0) {
		warn("failed to write strings");
		ret = n;
		goto err;
	}

	ret = openat(ffs_fd, USB_FFS_OUT_NAME, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (ret < 0) {
		warn("cannot open bulk-out ep");
		goto err;
	}
	h->bulk_out = ret;

	ret = openat(ffs_fd, USB_FFS_IN_NAME, O_RDWR);
	if (ret < 0) {
		warn("cannot open bulk-in ep");
		goto err;
	}
	h->bulk_in = ret;

	close(ffs_fd);

	return 0;

err:
	if (ffs_fd > 0)
		close(ffs_fd);

	if (h->bulk_in > 0) {
		close(h->bulk_in);
		h->bulk_in = -1;
	}

	if (h->bulk_out > 0) {
		close(h->bulk_out);
		h->bulk_out = -1;
	}

	if (h->control > 0) {
		close(h->control);
		h->control = -1;
	}

	return ret;
}

static struct usb_handle *usb_ffs_init()
{
	struct usb_handle *h = calloc(1, sizeof(struct usb_handle));

	if (h == NULL) {
		warn("couldn't allocate usb_handle");
		return h;
	}

	h->control = -1;
	h->bulk_out = -1;
	h->bulk_in = -1;

	if (init_functionfs(h)) {
		free(h);
		h = NULL;
	}

	return h;
}

static int enable_udc(bool enable)
{
	int gadget_fd, udc_fd;
	const char* udcname = "";

	if (enable)
		udcname = g_usb_config.udc_name;

	gadget_fd = open(g_usb_config.gadget_path, O_DIRECTORY);
	if (gadget_fd < 0)
		return gadget_fd;

	udc_fd = openat(gadget_fd, UDC_ATTRIB, O_WRONLY);
	if (udc_fd < 0)
		err(1, "failed to open UDC\n");


	write(udc_fd, udcname, strlen(udcname) + 1);

	close(udc_fd);
	close(gadget_fd);

	return 0;
}

int diag_usb_open(const char *usbname, const char *serial)
{
	struct usb_handle *handle = NULL;

	if (usbname && usbname[0])
		g_usb_config.dev_name = usbname;

	if (serial && serial[0])
		strncpy(g_usb_config.serial, serial, sizeof(g_usb_config.serial) - 1);

	handle = usb_ffs_init();
	if (handle == NULL)
		return -1;

	if (enable_udc(true)) {
		close(handle->control);
		close(handle->bulk_out);
		close(handle->bulk_in);
		free(handle);
		return -1;
	}

	printf("Connected to %s %s\n", g_usb_config.dev_name, g_usb_config.serial);

	dm_add("USB client", handle->bulk_in, handle->bulk_out);

	return handle->control;
}
