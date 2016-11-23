/*
 * Copyright (c) 2016, Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <stdlib.h>
#include <string.h>
#include "mbuf.h"

struct mbuf *mbuf_alloc(size_t size)
{
	struct mbuf *mbuf;

	mbuf = malloc(sizeof(*mbuf) + size);
	if (!mbuf)
		return NULL;

	memset(mbuf, 0, sizeof(*mbuf));
	mbuf->size = size;

	return mbuf;
}

void *mbuf_put(struct mbuf *mbuf, size_t size)
{
	void *ptr;

	if (mbuf->offset + size > mbuf->size)
		return NULL;

	ptr = mbuf->data + mbuf->offset;
	mbuf->offset += size;

	return ptr;
}
