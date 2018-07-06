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
#include <sys/time.h>
#include <sys/types.h>

#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "list.h"
#include "mbuf.h"
#include "util.h"
#include "watch.h"

struct watch {
	int fd;
	int (*cb)(int, void*);
	void *data;
	struct list_head *queue;

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
static struct list_head write_watches = LIST_INIT(write_watches);
static struct list_head quit_watches = LIST_INIT(quit_watches);
static bool do_watch_quit;

int watch_add_readfd(int fd, int (*cb)(int, void*), void *data)
{
	struct watch *w;

	w = calloc(1, sizeof(struct watch));
	if (!w)
		err(1, "calloc");

	w->fd = fd;
	w->cb = cb;
	w->data = data;

	list_add(&read_watches, &w->node);

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

	list_add(&write_watches, &w->node);

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

	list_for_each_safe(item, next, &write_watches) {
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
	struct list_head *pos;
	struct timeval tv;
	struct timer *selected;
	struct timer *timer;

	if (list_empty(&timers))
		return NULL;

	selected = container_of(timers.next, struct timer, node);
	tv = selected->tick;

	list_for_each(pos, &timers) {
		timer = container_of(pos, struct timer, node);

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

static void handle_write(struct watch *w)
{
	struct list_head *item;
	struct list_head *next;
	struct mbuf *mbuf;
	ssize_t n;

	for (item = w->queue->next; item != w->queue; item = next) {
		next = item->next;

		mbuf = container_of(item, struct mbuf, node);

		n = write(w->fd, mbuf->data, mbuf->size);
		if (n < 0)
			break;

		list_del(&mbuf->node);
		free(mbuf);
	}
}

void watch_run(void)
{
	struct timer *timer;
	struct timeval now;
	struct timeval tv;
	struct watch *w;
	fd_set rfds;
	fd_set wfds;
	int nfds;
	int ret;
	struct list_head *item;

	while (!do_watch_quit) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		nfds = 0;

		list_for_each(item, &read_watches) {
			w = container_of(item, struct watch, node);
			FD_SET(w->fd, &rfds);

			nfds = MAX(w->fd + 1, nfds);
		}

		list_for_each(item, &write_watches) {
			w = container_of(item, struct watch, node);
			if (!list_empty(w->queue)) {
				FD_SET(w->fd, &wfds);

				nfds = MAX(w->fd + 1, nfds);
			}
		}

		timer = watch_get_next_timer();
		if (timer) {
			gettimeofday(&now, NULL);
			timersub(&timer->tick, &now, &tv);

			if (tv.tv_sec < 0)
				tv.tv_sec = tv.tv_usec = 0;
		} else {
			tv.tv_sec = 10;
			tv.tv_usec = 0;
		}

		ret = select(nfds, &rfds, &wfds, NULL, &tv);
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

		list_for_each(item, &read_watches) {
			w = container_of(item, struct watch, node);
			if (FD_ISSET(w->fd, &rfds)) {
				ret = w->cb(w->fd, w->data);
				if (ret < 0) {
					do_watch_quit = true;
					break;
				}
			}
		}

		list_for_each(item, &write_watches) {
			w = container_of(item, struct watch, node);
			if (FD_ISSET(w->fd, &wfds))
				handle_write(w);
		}
	}

	list_for_each(item, &quit_watches) {
		w = container_of(item, struct watch, node);
		w->cb(-1, w->data);
	}
}
