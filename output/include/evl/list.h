/*
 * SPDX-License-Identifier: MIT
 *
 * Imported from Xenomai 'list' macros (https://xenomai.org/)
 * Copyright (C) 2019 Philippe Gerum <rpm@xenomai.org>
 * Relicensed by its author from LGPL 2.1 to MIT.
 */

#ifndef _EVL_LIST_H
#define _EVL_LIST_H

#include <evl/compiler.h>

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD_INITIALIZER(__name)	\
	{ .next = &(__name), .prev = &(__name) }

#define DEFINE_LIST_HEAD(__name) \
	struct list_head __name = LIST_HEAD_INITIALIZER(__name)

static inline void inith(struct list_head *head)
{
	head->next = head;
	head->prev = head;
}

static inline void ath(struct list_head *head,
		       struct list_head *e)
{
	/* Inserts the new element right after the heading one. */
	e->prev = head;
	e->next = head->next;
	e->next->prev = e;
	head->next = e;
}

static inline void dth(struct list_head *e)
{
	e->prev->next = e->next;
	e->next->prev = e->prev;
}

static inline void list_init(struct list_head *head)
{
	inith(head);
}

/*
 * XXX: list_init() is mandatory if you later want to use this
 * predicate.
 */
static inline int list_is_linked(const struct list_head *e)
{
	return !(e->prev == e->next && e->prev == e);
}

static inline void list_prepend(struct list_head *e,
				struct list_head *head)
{
	ath(head, e);
}

static inline void list_append(struct list_head *e,
			       struct list_head *head)
{
	ath(head->prev, e);
}

static inline void list_insert(struct list_head *next,
			       struct list_head *prev)
{
	ath(prev, next);
}

static inline void list_join(struct list_head *src,
			     struct list_head *dst)
{
	struct list_head *headsrc = src->next;
	struct list_head *tailsrc = src->prev;

	headsrc->prev->next = tailsrc->next;
	tailsrc->next->prev = headsrc->prev;
	headsrc->prev = dst;
	tailsrc->next = dst->next;
	dst->next->prev = tailsrc;
	dst->next = headsrc;
}

static inline void list_remove(struct list_head *e)
{
	dth(e);
}

static inline void list_remove_init(struct list_head *e)
{
	dth(e);
	inith(e);
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

static inline struct list_head *list_pop(struct list_head *head)
{
	struct list_head *e = head->next;
	list_remove(e);
	return e;
}

static inline int list_is_heading(const struct list_head *e,
				  const struct list_head *head)
{
	return head->next == e;
}

#define list_entry(ptr, type, member)			\
	container_of(ptr, type, member)

#define list_first_entry(head, type, member)		\
	list_entry((head)->next, type, member)

#define list_last_entry(head, type, member)		\
	list_entry((head)->prev, type, member)

#define list_prev_entry(pos, head, member)				\
	({								\
		typeof(*pos) *__prev = NULL;				\
		if ((head)->next != &(pos)->member)			\
			__prev = list_entry((pos)->member.prev,		\
					    typeof(*pos), member);	\
		__prev;							\
	})

#define list_next_entry(pos, head, member)				\
	({								\
		typeof(*pos) *__next = NULL;				\
		if ((head)->prev != &(pos)->member)			\
			__next = list_entry((pos)->member.next,		\
					      typeof(*pos), member);	\
		__next;							\
	})

#define list_pop_entry(head, type, member)				\
	({								\
		struct list_head *__e = list_pop(head);			\
		list_entry(__e, type, member);				\
	})

#define list_for_each(pos, head)					\
	for (pos = (head)->next;					\
	     pos != (head); pos = (pos)->next)

#define list_for_each_reverse(pos, head)				\
	for (pos = (head)->prev;					\
	     pos != (head); pos = (pos)->prev)

#define list_for_each_safe(pos, tmp, head)				\
	for (pos = (head)->next, tmp = (pos)->next;			\
	     pos != (head); pos = tmp, tmp = (pos)->next)

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next,				\
			      typeof(*pos), member);			\
	     &(pos)->member != (head);					\
	     pos = list_entry((pos)->member.next,			\
			      typeof(*pos), member))

#define list_for_each_entry_safe(pos, tmp, head, member)		\
	for (pos = list_entry((head)->next,				\
			      typeof(*pos), member),			\
		     tmp = list_entry((pos)->member.next,		\
				      typeof(*pos), member);		\
	     &(pos)->member != (head);					\
	     pos = tmp, tmp = list_entry((pos)->member.next,		\
					 typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_entry((head)->prev,				\
			      typeof(*pos), member);			\
	     &pos->member != (head);					\
	     pos = list_entry(pos->member.prev,				\
			      typeof(*pos), member))

#define list_for_each_entry_reverse_safe(pos, tmp, head, member)	\
	for (pos = list_entry((head)->prev,				\
			      typeof(*pos), member),			\
		     tmp = list_entry((pos)->member.prev,		\
				      typeof(*pos), member);		\
	     &(pos)->member != (head);					\
	     pos = tmp, tmp = list_entry((pos)->member.prev,		\
					 typeof(*pos), member))

#endif /* !_EVL_LIST_H */
