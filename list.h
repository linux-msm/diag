#ifndef __LIST_H__
#define __LIST_H__

#include <stdbool.h>
#include <stddef.h>

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#define LIST_INIT(list) { &(list), &(list) }

static inline void list_init(struct list_head *list)
{
	list->prev = list->next = list;
}

static inline bool list_empty(struct list_head *list)
{
	return list->next == list;
}

static inline void list_add(struct list_head *list, struct list_head *item)
{
	struct list_head *prev = list->prev;

	item->next = list;
	item->prev = prev;

	prev->next = list->prev = item;
}

static inline void list_del(struct list_head *item)
{
	item->prev->next = item->next;
	item->next->prev = item->prev;
}

#define list_for_each(item, list) \
	for (item = (list)->next; item != list; item = item->next)

#define list_for_each_safe(item, next, list) \
	for (item = (list)->next, next = item->next; item != list; item = next, next = item->next)

#endif
