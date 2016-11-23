#ifndef __WATCH_H__
#define __WATCH_H__

#include <stdbool.h>
#include "list.h"

int watch_add_readfd(int fd, int (*cb)(int, void*), void *data);
int watch_add_writeq(int fd, struct list_head *queue);
int watch_add_quit(int (*cb)(int, void*), void *data);
int watch_add_timer(void (*cb)(void *), void *data,
		    unsigned int interval, bool repeat);
void watch_quit(void);
void watch_run(void);

#endif
