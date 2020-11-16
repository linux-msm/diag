/*
 * Copyright (c) 2016, Bjorn Andersson <bjorn@kryo.se>
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
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>

#include <linux/aio_abi.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "list.h"
#include "mbuf.h"
#include "util.h"
#include "watch.h"

#define FLOW_WATERMARK	10

/**
 * struct watch_flow - flow control context
 * @packets: number of outstanding packets
 */
struct watch_flow {
	int packets;
};

struct watch {
	int fd;
	int (*cb)(int, void*);
	void *data;
	struct list_head *queue;

	struct iocb iocb;
	struct mbuf *pending_aio;

	bool is_write;

	struct watch_flow *flow;

	int (*aio_complete)(struct mbuf *, void*);

	struct list_head node;
};

struct timer {
	void (*cb)(void *);
	void *data;
	unsigned int interval;
	bool repeat;

	struct timeval tick;

	struct list_head node;
};

static struct list_head timers = LIST_INIT(timers);

static struct list_head read_watches = LIST_INIT(read_watches);
static struct list_head aio_watches = LIST_INIT(aio_watches);
static struct list_head quit_watches = LIST_INIT(quit_watches);
static bool do_watch_quit;

typedef unsigned long aio_context_t;

static long io_destroy(aio_context_t ctx)
{
	return syscall(__NR_io_destroy, ctx);
}

static long io_getevents(__attribute__((unused)) aio_context_t ctx,
			 __attribute__((unused)) long min_nr,
			 __attribute__((unused)) long nr,
			 __attribute__((unused)) struct io_event *events,
			 __attribute__((unused)) struct timespec *tmo)
{
#ifdef __NR_io_getevents
	return syscall(__NR_io_getevents, ctx, min_nr, nr, events, tmo);
#else
	return -ENOSYS;
#endif
}

static long io_setup(unsigned nr_reqs, aio_context_t *ctx)
{
	return syscall(__NR_io_setup, nr_reqs, ctx);
}

static long io_submit(aio_context_t ctx, long n, struct iocb **paiocb)
{
	return syscall(__NR_io_submit, ctx, n, paiocb);
}

struct watch_flow *watch_flow_new(void)
{
	return calloc(1, sizeof(struct watch_flow));
}

void watch_flow_inc(struct watch_flow *flow)
{
	if (!flow)
		return;

	flow->packets++;
}

static void watch_flow_dec(struct watch_flow *flow)
{
	if (!flow)
		return;

	if (!flow->packets)
		fprintf(stderr, "unbalanced flow control\n");
	else
		flow->packets--;
}

static bool watch_flow_blocked(struct watch_flow *flow)
{
	return flow && flow->packets > FLOW_WATERMARK;
}

int watch_add_readfd(int fd, int (*cb)(int, void*), void *data,
		     struct watch_flow *flow)
{
	struct watch *w;

	w = calloc(1, sizeof(struct watch));
	if (!w)
		err(1, "calloc");

	w->fd = fd;
	w->cb = cb;
	w->data = data;
	w->flow = flow;

	list_add(&read_watches, &w->node);

	return 0;
}

int watch_add_readq(int fd, struct list_head *queue,
		    int (*cb)(struct mbuf *mbuf, void *data), void *data)
{
	struct watch *w;

	w = calloc(1, sizeof(*w));
	if (!w)
		err(1, "calloc");

	w->fd = fd;
	w->aio_complete = cb;
	w->data = data;
	w->queue = queue;

	w->is_write = false;

	list_add(&aio_watches, &w->node);

	return 0;
}

static int watch_free_write_aio(struct mbuf *mbuf, void *data)
{
	watch_flow_dec(mbuf->flow);
	free(mbuf);

	return 0;
}

int watch_add_writeq(int fd, struct list_head *queue)
{
	struct watch *w;

	w = calloc(1, sizeof(*w));
	if (!w)
		err(1, "calloc");

	w->fd = fd;
	w->queue = queue;
	w->data = w;

	w->aio_complete = watch_free_write_aio;

	w->is_write = true;

	list_add(&aio_watches, &w->node);

	return 0;
}

void watch_remove_fd(int fd)
{
	struct list_head *item;
	struct list_head *next;
	struct watch *w;

	list_for_each_safe(item, next, &read_watches) {
		w = container_of(item, struct watch, node);
		if (w->fd == fd) {
			list_del(&w->node);
			free(w);
		}
	}

	list_for_each_safe(item, next, &aio_watches) {
		w = container_of(item, struct watch, node);
		if (w->fd == fd) {
			list_del(&w->node);
			free(w);
		}
	}
}

void watch_remove_writeq(int fd)
{
	struct list_head *item;
	struct list_head *next;
	struct watch *w;

	list_for_each_safe(item, next, &aio_watches) {
		w = container_of(item, struct watch, node);
		if (w->fd == fd) {
			list_del(&w->node);
			free(w);
		}
	}
}

int watch_add_quit(int (*cb)(int, void*), void *data)
{
	struct watch *w;

	w = calloc(1, sizeof(struct watch));
	if (!w)
		err(1, "calloc");

	w->cb = cb;
	w->data = data;

	list_add(&quit_watches, &w->node);

	return 0;
}

static void watch_set_timer(struct timer *timer)
{
	struct timeval now;
	struct timeval tv;
	int ret;

	ret = gettimeofday(&now, NULL);
	if (ret < 0)
		err(1, "failed to gettimeofday");

	tv.tv_sec = timer->interval / 1000;
	tv.tv_usec = (timer->interval % 1000) * 1000;

	timeradd(&now, &tv, &timer->tick);
}

int watch_add_timer(void (*cb)(void *), void *data, unsigned int interval,
		    bool repeat)
{
	struct timer *w;

	w = calloc(1, sizeof(struct timer));
	if (!w)
		err(1, "calloc");

	w->cb = cb;
	w->data = data;
	w->interval = interval;
	w->repeat = repeat;

	list_add(&timers, &w->node);

	watch_set_timer(w);

	return 0;
}

static void watch_free_timer(struct timer *timer)
{
	list_del(&timer->node);
	free(timer);
}

static struct timer *watch_get_next_timer()
{
	struct timeval tv;
	struct timer *selected;
	struct timer *timer;

	if (list_empty(&timers))
		return NULL;

	selected = container_of(timers.next, struct timer, node);
	tv = selected->tick;

	list_for_each_entry(timer, &timers, node) {
		if (timercmp(&timer->tick, &tv, <)) {
			selected = timer;
			tv = timer->tick;
		}
	}

	return selected;
}

void watch_quit(void)
{
	do_watch_quit = true;
}

static void watch_submit_aio(aio_context_t ioctx, int evfd, struct watch *w)
{
	struct iocb *iocb = &w->iocb;
	struct mbuf *mbuf;
	int ret;

	assert(!w->pending_aio);

	if (list_empty(w->queue))
		return;

	mbuf = list_entry_first(w->queue, struct mbuf, node);

	memset(iocb, 0, sizeof(*iocb));
	iocb->aio_fildes = w->fd;
	iocb->aio_lio_opcode = w->is_write ? IOCB_CMD_PWRITE : IOCB_CMD_PREAD;
	iocb->aio_buf = (uint64_t)mbuf->data;
	iocb->aio_nbytes = mbuf->size;
	iocb->aio_offset = 0;
	iocb->aio_flags = IOCB_FLAG_RESFD;
	iocb->aio_resfd = evfd;

	ret = io_submit(ioctx, 1, &iocb);
	if (ret != 1)
		fprintf(stderr, "io_submit failed: %d (%d)\n", ret, errno);

	if (ret == 1) {
		list_del(&mbuf->node);
		w->pending_aio = mbuf;
	}
}

static void watch_handle_eventfd(int evfd, aio_context_t ioctx)
{
	struct io_event ev[32];
	struct iocb *iocb;
	struct watch *next;
	struct watch *w;
	uint64_t evcnt;
	ssize_t n;
	int count;
	int i;

	n = read(evfd, &evcnt, sizeof(evcnt));
	if (n < 0) {
		warn("failed to read eventfd counter");
		return;
	}

	count = io_getevents(ioctx, 1, 32, ev, NULL);
	list_for_each_entry_safe(w, next, &aio_watches, node) {
		for (i = 0; i < count; i++) {
			iocb = (struct iocb *)ev[i].obj;
			if (iocb->aio_fildes == w->fd) {
				assert(w->pending_aio);

				if (ev[i].res == -EAGAIN)
					continue;

				if (!w->is_write && ev[i].res >= 0)
					w->pending_aio->offset = ev[i].res;

				w->aio_complete(w->pending_aio, w->data);
				w->pending_aio = NULL;

				// watch_submit_aio(ioctx, evfd, w);
			}
		}
	}
}

void watch_run(void)
{
	struct timeval *timeout;
	struct timer *timer;
	struct timeval now;
	struct timeval tv;
	aio_context_t ioctx = 0;
	struct watch *next;
	struct watch *w;
	fd_set rfds;
	int evfd;
	int nfds;
	int ret;

	evfd = eventfd(0, 0);
	if (evfd < 0)
		err(1, "failed to create eventfd");

	ret = io_setup(32, &ioctx);
	if (ret < 0)
		err(1, "failed to initialize aio context");

	while (!do_watch_quit) {
		FD_ZERO(&rfds);
		FD_SET(evfd, &rfds);

		nfds = evfd + 1;

		list_for_each_entry(w, &read_watches, node) {
			/* Skip read watches with flows that are blocked */
			if (watch_flow_blocked(w->flow))
				continue;

			FD_SET(w->fd, &rfds);

			nfds = MAX(w->fd + 1, nfds);
		}

		list_for_each_entry(w, &aio_watches, node) {
			/* Submit AIO if none is pending */
			if (!list_empty(w->queue) && !w->pending_aio)
				watch_submit_aio(ioctx, evfd, w);
		}

		timer = watch_get_next_timer();
		if (timer) {
			gettimeofday(&now, NULL);
			timersub(&timer->tick, &now, &tv);

			if (tv.tv_sec < 0)
				tv.tv_sec = tv.tv_usec = 0;

			timeout = &tv;
		} else {
			timeout = NULL;
		}

		ret = select(nfds, &rfds, NULL, NULL, timeout);
		if (ret < 0) {
			warn("failed to select");
			break;
		}

		if (ret == 0 && timer) {
			timer->cb(timer->data);

			if (timer->repeat)
				watch_set_timer(timer);
			else
				watch_free_timer(timer);
		}

		if (FD_ISSET(evfd, &rfds))
			watch_handle_eventfd(evfd, ioctx);

		list_for_each_entry_safe(w, next, &read_watches, node) {
			if (FD_ISSET(w->fd, &rfds)) {
				ret = w->cb(w->fd, w->data);
				if (ret < 0)
					list_del(&w->node);
			}
		}
	}

	list_for_each_entry(w, &quit_watches, node)
		w->cb(-1, w->data);

	io_destroy(ioctx);
}
