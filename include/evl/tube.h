/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 *
 * The tube: a lighweight, lockless multi-reader/multi-writer FIFO
 * with a base-offset addressing variant which can work over a memory
 * segment shared between processes (*_rel form).
 */

#ifndef _EVL_TUBE_H
#define _EVL_TUBE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <evl/compiler.h>
#include <evl/atomic.h>

#define DECLARE_CANISTER_QUEUE(__can_struct)	\
	struct {				\
		struct __can_struct *head;	\
		struct __can_struct *tail;	\
		struct __can_struct first;	\
	}

#define CANISTER_QUEUE_INITIALIZER(__name)	\
	{					\
		.head = &(__name).first,	\
		.tail = &(__name).first,	\
		.first = {			\
			.next = NULL,		\
		},				\
	}

#define tube_push_prepare(__desc, __free)			\
	({							\
		typeof((__desc)->tail) __old_qp;		\
		atomic_store(&(__free)->next, NULL);		\
		smp_mb();	/* (1) */			\
		do {						\
			__old_qp = (__desc)->tail;		\
		} while (__sync_val_compare_and_swap(		\
				&(__desc)->tail,		\
				__old_qp, __free) != __old_qp);	\
		__old_qp;					\
	})

/*
 * We may directly compete with tube_push_prepare() on the tail
 * pointer, and indirectly with tube_pull() which may be walking the
 * canister list through the ->next link. A NULL link denotes the end
 * canister, tube_pull() cannot walk past that one, and never dequeues
 * it. Since __new was the previous end canister referred to by the
 * tail pointer, __new->next is NULL until sending is completed.
 */
#define tube_push_finish(__desc, __new, __free)		\
	do {						\
		smp_mb();	/* (1) */		\
		atomic_store(&(__new)->next, __free);	\
	} while (0)

#define tube_push(__desc, __free)				\
	do {							\
		typeof((__desc)->tail) __new_q;			\
		__new_q = tube_push_prepare(__desc, __free);	\
		tube_push_finish(__desc, __new_q, __free);	\
	} while (0)

#define tube_push_item(__desc, __item, __free)			\
	do {							\
		typeof((__desc)->tail) __new_qi;		\
		__new_qi = tube_push_prepare(__desc, __free);	\
		__new_qi->payload = (__item);			\
		tube_push_finish(__desc, __new_qi, __free);	\
	} while (0)

#define tube_pull(__desc)						\
	({								\
		typeof((__desc)->head) __next_dq, __old_dq;		\
		do {							\
			__old_dq = (__desc)->head;			\
			__next_dq = __old_dq->next;			\
		} while (__next_dq && __sync_val_compare_and_swap(	\
				&(__desc)->head,			\
				__old_dq, __next_dq) != __old_dq);	\
		__next_dq ? __old_dq : NULL;				\
	})

#define DECLARE_EVL_TUBE_CANISTER(__can_struct, __payload)	\
	struct __can_struct {					\
		typeof(__payload) payload;			\
		struct __can_struct *next;			\
	}

#define DECLARE_EVL_TUBE(__name, __can_struct)			\
	struct __name {						\
		DECLARE_CANISTER_QUEUE(__can_struct) pending;	\
		DECLARE_CANISTER_QUEUE(__can_struct) free;	\
		long max_items;					\
	}

#define TUBE_INITIALIZER(__name)					\
	{								\
		.pending = CANISTER_QUEUE_INITIALIZER((__name).pending), \
		.free = CANISTER_QUEUE_INITIALIZER((__name).free),	\
		.max_items = 0,						\
	}

#define evl_get_tube_size(__name, __count)			\
	({							\
		struct __name *__ptr;				\
		sizeof(struct __name) +				\
			sizeof(__ptr->free.first) * __count;	\
	})

#define evl_init_tube(__tube, __freevec, __count)		\
	do {							\
		typeof((__tube)->pending.tail) __i;		\
		int __n;					\
		*(__tube) = (typeof(*(__tube)))			\
			TUBE_INITIALIZER(*(__tube));		\
		for (__n = 0, __i = (typeof(__i))(__freevec);	\
		     __n < (__count); __n++, __i++)		\
			tube_push(&(__tube)->free, __i);	\
		(__tube)->max_items = (__count);		\
	} while (0)

#define evl_send_tube(__tube, __item)				\
	({							\
		bool __ret = false;				\
		typeof((__tube)->pending.tail) __new;		\
		__new = tube_pull(&(__tube)->free);		\
		if (likely(__new)) {				\
			tube_push_item(&(__tube)->pending,	\
				__item, __new);			\
			__ret = true;				\
		}						\
		__ret;						\
	})

#define evl_receive_tube(__tube, __item)			\
	({							\
		bool __ret = false;				\
		typeof((__tube)->pending.tail) __next;		\
		__next = tube_pull(&(__tube)->pending);	\
		if (__next) {					\
			__item = __next->payload;		\
			tube_push(&(__tube)->free, __next);	\
			__ret = true;				\
		}						\
		__ret;						\
	})

/*
 * Position-independent variant.
 */

#define DECLARE_TUBE_CANISTER_REL(__can_struct, __payload)	\
	struct __can_struct {					\
		typeof(__payload) payload;			\
		uintptr_t next;					\
	}							\

#define DECLARE_CANISTER_QUEUE_REL(__can_struct)		\
	struct {						\
		uintptr_t head;					\
		uintptr_t tail;					\
		/* Must NOT be first (non-zero offset) */	\
		struct __can_struct first[1];			\
	}

#define canq_offsetof(__baseoff, __can_struct, __member)	\
	(offsetof(typeof(DECLARE_CANISTER_QUEUE_REL(__can_struct)), __member) + __baseoff)

#define CANISTER_QUEUE_INITIALIZER_REL(__baseoff, __can_struct)		\
	{								\
		.head = canq_offsetof(__baseoff, __can_struct, first),	\
		.tail = canq_offsetof(__baseoff, __can_struct, first),	\
		.first = {						\
			[0] = {						\
				.next = 0,				\
			},						\
		},							\
	}

#define tube_push_prepare_rel(__base, __desc, __free)			\
	({								\
		typeof((__desc)->tail) __old_qp;			\
		uintptr_t __off_qp = __memoff(__base, __free);		\
		atomic_store(&(__free)->next, 0);			\
		smp_mb();	/* (1) */				\
		do {							\
			__old_qp = (__desc)->tail;			\
		} while (__sync_val_compare_and_swap(			\
				&(__desc)->tail,			\
				__old_qp, __off_qp) != __old_qp);	\
		__memptr(__base, __old_qp);				\
	})

#define tube_push_finish_rel(__base, __desc, __new, __free)		\
	do {								\
		smp_mb();	/* (1) */				\
		atomic_store(&(__new)->next, __memoff(__base, __free));	\
	} while (0)

#define tube_push_rel(__base, __desc, __free)				\
	do {								\
		typeof((__desc)->first[0]) *__new_q;			\
		__new_q = tube_push_prepare_rel(__base, __desc, __free); \
		tube_push_finish_rel(__base, __desc, __new_q, __free); \
	} while (0)

#define tube_push_item_rel(__base, __desc, __item, __free)		\
	do {								\
		typeof((__desc)->first[0]) *__new_qi;			\
		__new_qi = tube_push_prepare_rel(__base, __desc, __free); \
		__new_qi->payload = (__item);				\
		tube_push_finish_rel(__base, __desc, __new_qi, __free); \
	} while (0)

#define tube_pull_rel(__base, __desc)					\
	({								\
		typeof((__desc)->head) __next_dq, __old_dq;		\
		typeof((__desc)->first[0]) *__old_dqptr;		\
		do {							\
			__old_dq = (__desc)->head;			\
			__old_dqptr = __memptr(__base, __old_dq);	\
			__next_dq = __old_dqptr->next;			\
		} while (__next_dq && __sync_val_compare_and_swap(	\
				&(__desc)->head,			\
				__old_dq, __next_dq) != __old_dq);	\
		__next_dq ? __old_dqptr : NULL;				\
	})

#define DECLARE_EVL_TUBE_REL(__name, __can_struct, __payload)		\
	DECLARE_TUBE_CANISTER_REL(__can_struct, __payload);		\
	struct __name {							\
		DECLARE_CANISTER_QUEUE_REL(__can_struct) pending;	\
		DECLARE_CANISTER_QUEUE_REL(__can_struct) free;		\
		long max_items;						\
	}

#define TUBE_INITIALIZER_REL(__name, __can_struct)			\
	{								\
		.pending = CANISTER_QUEUE_INITIALIZER_REL(__memoff(&(__name), \
				&(__name).pending), __can_struct), 	\
		.free = CANISTER_QUEUE_INITIALIZER_REL(__memoff(&(__name), \
				&(__name).free), __can_struct),		\
		.max_items = 0,						\
	}

#define evl_get_tube_size_rel(__name, __count)	\
	evl_get_tube_size(__name, __count)

#define evl_init_tube_rel(__name, __can_struct, __mem, __size)		\
	({								\
		struct __name *__tube = (__mem);			\
		typeof(__tube->free.first[0]) *__i, *__iend;		\
		*__tube = (typeof(*__tube))				\
			TUBE_INITIALIZER_REL(*__tube, __can_struct);	\
		__iend = (typeof(__iend))(__mem + __size);		\
		for (__i = (typeof(__i))(__tube + 1); __i < __iend; __i++) { \
			tube_push_rel(__tube, &(__tube)->free, __i);	\
		}							\
		__tube->max_items = __i - &__tube->free.first[1];	\
		__tube;							\
	})

#define evl_send_tube_rel(__tube, __item)			\
	({							\
		bool __ret = false;				\
		typeof((__tube)->pending.first[0]) *__new;	\
		__new = tube_pull_rel(__tube, &(__tube)->free);\
		if (likely(__new)) {				\
			tube_push_item_rel(__tube,		\
					&(__tube)->pending,	\
					__item, __new);		\
			__ret = true;				\
		}						\
		__ret;						\
	})

#define evl_receive_tube_rel(__tube, __item)				\
	({								\
		bool __ret = false;					\
		typeof((__tube)->pending.first[0]) *__next;		\
		__next = tube_pull_rel(__tube, &(__tube)->pending);	\
		if (__next) {						\
			__item = __next->payload;			\
			tube_push_rel(__tube, &(__tube)->free, __next); \
			__ret = true;					\
		}							\
		__ret;							\
	})

#endif /* _EVL_TUBE_H */
