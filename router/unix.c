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

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "dm.h"
#include "watch.h"

static int unix_listen(int fd, void *data)
{
	struct diag_client *dm;
	int client;
	int ret;

	client = accept(fd, NULL, NULL);
	if (client < 0) {
		fprintf(stderr, "failed to accept");
		return 0;
	}

	ret = fcntl(client, F_SETFL, O_NONBLOCK);
	if (ret < 0) {
		fprintf(stderr, "failed to set O_NONBLOCK");
		return 0;
	}

	dm = dm_add("UNIX", client, client, false);
	dm_enable(dm);

	return 0;
}

int diag_unix_open(void)
{
	struct sockaddr_un addr;
	int ret;
	int fd;

	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		fprintf(stderr, "failed to create unix socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "\0diag", sizeof(addr.sun_path)-1);
	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret < 0) {
		fprintf(stderr, "failed to bind diag socket");
		return -1;
	}

	ret = listen(fd, 2);
	if (ret < 0) {
		fprintf(stderr, "failed to listen on diag socket\n");
		return -1;
	}

	watch_add_readfd(fd, unix_listen, NULL, NULL);

	return 0;
}
