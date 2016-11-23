#ifndef __MBUF_H__
#define __MBUF_H__

#include "list.h"

struct mbuf {
	struct list_head node;

	size_t size;
	size_t offset;

	char data[];
};

struct mbuf *mbuf_alloc(size_t size);
void *mbuf_put(struct mbuf *mbuf, size_t size);

#endif
