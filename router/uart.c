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
#include <sys/ioctl.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "diag.h"
#include "dm.h"
#include "hdlc.h"
#include "watch.h"

#define APPS_BUF_SIZE 16384

static unsigned int check_baudrate(unsigned int baudrate)
{
	switch (baudrate)
	{
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 115200:
		return B115200;
	default:
		warn("Illegal baud rate %u!", baudrate);
		return 0;
	}
}

int diag_uart_open(const char *uartname, unsigned int baudrate)
{
	struct diag_client *dm;
	int ret;
	int fd;
	struct termios options, options_save;

	baudrate = check_baudrate(baudrate);
	if (baudrate == 0)
		return -EINVAL;

	fd = open(uartname, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	ret = tcflush(fd, TCIOFLUSH);
	if (ret < 0)
		return -errno;

	ret = fcntl(fd, F_SETFL, 0);
	if (ret < 0)
		return -errno;

	ret = ioctl(fd, TCGETS, &options_save);
	if (ret < 0)
		return -errno;

	options = options_save;
	options.c_cc[VTIME]    = 0;   /* inter-character timer unused */
	options.c_cc[VMIN]     = 4;   /* blocking read until 4 chars received */
	options.c_cflag &= ~PARENB;
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= (CS8 | CLOCAL | CREAD);
	options.c_iflag = 0;
	options.c_oflag = 0;
	options.c_lflag = 0;
	options.c_cflag = (options.c_cflag & ~CBAUD) | (baudrate & CBAUD);

	ret = ioctl(fd, TCSETS, &options); // TODO: need to call ioctl(ret, TCSETS, &options_save) to revert to original state
	if (ret < 0)
		return -errno;

	printf("Connected to %s@%d\n", uartname, baudrate);

	dm = dm_add("UART client", fd, fd, true);
	dm_enable(dm);

	return fd;
}
