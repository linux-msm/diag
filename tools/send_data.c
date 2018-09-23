/*
 * Copyright (c) 2018, Linaro Ltd.
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

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define DIAG_CMD_RSP_BAD_COMMAND			0x13
#define DIAG_CMD_RSP_BAD_PARAMS				0x14
#define DIAG_CMD_RSP_BAD_LENGTH				0x15

int main(int argc, char **argv)
{
	struct sockaddr_un addr;
	unsigned char *msg;
	struct timeval tv = {5, 0};
	fd_set rfds;
	ssize_t n;
	char buf[8192];
	int ret;
	int fd;
	int i;

	msg = calloc(argc - 1, sizeof(*msg));

	for (i = 1; i < argc; i++)
		msg[i - 1] = atoi(argv[i]);

	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0)
		err(1, "failed to create unix socket");

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "\0diag", sizeof(addr.sun_path)-1);

	ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret < 0)
		err(1, "failed to connect to diag");

	n = write(fd, msg, argc - 1);
	if (n < 0)
		err(1, "failed to send request");

	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		ret = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (!ret) {
			fprintf(stderr, "timeout waiting for response\n");
			exit(1);
		}

		if (!FD_ISSET(fd, &rfds))
			continue;

		n = read(fd, buf, sizeof(buf));
		if (!n) {
			fprintf(stderr, "disconnected\n");
			exit(1);
		} else if (n < 0) {
			fprintf(stderr, "failed to read response: %s\n",
				strerror(errno));
			exit(1);
		}

		if (buf[0] == DIAG_CMD_RSP_BAD_COMMAND) {
			printf("Diag response: Bad command\n");
			break;
		}

		for (i = 0; i < n; i++) {
			printf("%s%d", i == 0 ? "" : " ", buf[i]);
			if (i % 16 == 15 || i == n - 1)
				printf("\n");
		}

		if (buf[0] == msg[0])
			break;
	}

	return 0;
}

