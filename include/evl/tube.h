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

/*
 * The tricky one: pulling a canister from the tube. The noticeable
 * part is how we deal with the end-of-queue situation, temporarily
 * snatching the final element - which is not usable for carrying
 * payload and cannot be returned to the caller. We have two
 * requirements:
 *
 * - first is that a queue must contain at least one (final) element,
 * so that tube_push_item() can always store a new message there,
 * adding a free canister at the end of the same queue to compensate
 * for the consumed slot. Therefore, consuming the final element with
 * a NULL ->next pointer is not allowed, which means that observing
 * __desc->head->next == NULL denotes an empty queue.
 *
 * - second is to not interpret or dereference the value of __next_dq
 * until the CAS advancing the head pointer has taken place, so that
 * we don't consume such information which might have been updated in
 * parallel by a concurrent thread which won the CAS race on advancing
 * the same head pointer (i.e. __desc->head = __head_dq->next). We
 * solve this by allowing the head pointer to go NULL shortly,
 * reverting this move only after the CAS has succeeded, which
 * guarantees us that nobody else was granted the same item
 * (__head_dq). Another concurrent pull might fetch a NULL head in the
 * meantime, which also denotes an empty queue (like seeing
 * __head_dq->next == NULL).
 *
 * The only drawback of this scheme is as follows:
 *
 * A: tube_pull() sets head = NULL        B: tube_push() stores to final element
 *                                        B: tube_pull() observes empty queue (head == NULL)
 * A: tube_pull() restores head = &final
 *
 * IOW, when a puller reaches the non-removable final element of a
 * queue, a concurrent pusher might still observe an empty queue after
 * a successful push, until the puller eventually restores the head
 * pointer, revealing the new message. Although not perfect, that
 * seems acceptable from the standpoint of the pusher knowing that
 * concurrent pullers are present.
 */
#define tube_pull(__desc)						\
	({								\
		typeof((__desc)->head) __next_dq, __head_dq;		\
		do {							\
			__head_dq = atomic_load(&(__desc)->head);	\
			if (__head_dq == NULL)				\
				break;					\
			__next_dq = atomic_load(&(__head_dq)->next);	\
		} while (!__sync_bool_compare_and_swap(			\
				&(__desc)->head, __head_dq, __next_dq)); \
		if (__head_dq && __next_dq == NULL) {			\
			atomic_store(&(__desc)->head, __head_dq);	\
			__head_dq = NULL;				\
		}							\
		__head_dq;						\
	})

/*
 * The push operation moves the tail to a free canister, which serves
 * as the new final element, returning the former tail element which
 * will convey the payload (CAS ensures that multiple callers cannot
 * get the same element).
 *
 * FIXME: we still have a queue connectivity issue there if preempted
 * indefinitely between the prep and finish ops. Revisit.
 */
#define tube_push_prepare(__desc, __free)				\
  ({									\
		typeof((__desc)->tail) __old_qp;			\
		atomic_store(&(__free)->next, NULL);			\
		smp_mb();						\
		do {							\
			__old_qp = atomic_load(&(__desc)->tail);	\
		} while (!__sync_bool_compare_and_swap(			\
				&(__desc)->tail, __old_qp, __free));	\
		__old_qp;						\
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
		smp_mb();				\
		atomic_store(&(__new)->next, __free);	\
	} while (0)

#define tube_push(__desc, __free)				\
	do {							\
		typeof((__desc)->tail) __new_q;			\
		compiler_barrier();				\
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

#define DECLARE_EVL_CANISTER(__can_struct, __payload)		\
	struct __can_struct {					\
		struct __can_struct *next;			\
		typeof(__payload) payload;			\
	}

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
		if (__new) {					\
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

#define tube_push_prepare_rel(__base, __desc, __free)			\
	({								\
		typeof((__desc)->tail) __old_qp;			\
		uintptr_t __off_qp = __memoff(__base, __free);		\
		atomic_store(&(__free)->next, 0);			\
		smp_mb();						\
		do {							\
			__old_qp = atomic_load(&(__desc)->tail);	\
		} while (__sync_val_compare_and_swap(			\
				&(__desc)->tail,			\
				__old_qp, __off_qp) != __old_qp);	\
		__memptr(__base, __old_qp);				\
	})

/* See tube_pull() for details. */
#define tube_pull_rel(__base, __desc)					\
	({								\
		typeof((__desc)->head) __next_dq, __head_dq;		\
		typeof((__desc)->first[0]) *__head_dqptr;		\
		do {							\
			__head_dq = atomic_load(&(__desc)->head);	\
			if (__head_dq == 0) {				\
				__head_dqptr = NULL;			\
				break;					\
			}						\
			__head_dqptr = (typeof(__head_dqptr))		\
				__memptr(__base, __head_dq);		\
			__next_dq = atomic_load(&(__head_dqptr)->next);	\
		} while (!__sync_bool_compare_and_swap(			\
				&(__desc)->head, __head_dq, __next_dq)); \
		if (__head_dq && __next_dq == 0) {			\
			atomic_store(&(__desc)->head, __head_dq);	\
			__head_dqptr = NULL;				\
		}							\
		__head_dqptr;						\
	})

#define tube_push_finish_rel(__base, __desc, __new, __free)		\
	do {								\
		smp_mb();						\
		atomic_store(&(__new)->next, __memoff(__base, __free));	\
	} while (0)

#define tube_push_rel(__base, __desc, __free)				\
	do {								\
		typeof((__desc)->first[0]) *__new_q;			\
		compiler_barrier();					\
		__new_q = (typeof(__new_q))				\
			tube_push_prepare_rel(__base, __desc, __free);	\
		tube_push_finish_rel(__base, __desc, __new_q, __free);	\
	} while (0)

#define tube_push_item_rel(__base, __desc, __item, __free)		\
	do {								\
		typeof((__desc)->first[0]) *__new_qi;			\
		__new_qi = (typeof(__new_qi))				\
			tube_push_prepare_rel(__base, __desc, __free);	\
		__new_qi->payload = (__item);				\
		tube_push_finish_rel(__base, __desc, __new_qi, __free); \
	} while (0)

#define DECLARE_EVL_CANISTER_REL(__can_struct, __payload)	\
	struct __can_struct {					\
		uintptr_t next;					\
		typeof(__payload) payload;			\
	}							\

#define DECLARE_CANISTER_QUEUE_REL(__can_struct)		\
	struct {						\
		uintptr_t head;					\
		uintptr_t tail;					\
		/* Must NOT be first (non-zero offset) */	\
		struct __can_struct first[1];			\
	}

#define DECLARE_EVL_TUBE_REL(__name, __can_struct, __payload)		\
	struct __name {							\
		DECLARE_CANISTER_QUEUE_REL(__can_struct) pending;	\
		DECLARE_CANISTER_QUEUE_REL(__can_struct) free;		\
		long max_items;						\
	}

#define canq_offsetof(__baseoff, __can_struct, __member)		\
	({								\
		DECLARE_CANISTER_QUEUE_REL(__can_struct) __tmp;		\
		(void)(__tmp);	/* Pedantic indirection for C++ */	\
		offsetof(typeof(__tmp), __member) + __baseoff;		\
	})

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

#define TUBE_INITIALIZER_REL(__name, __can_struct)			\
	{								\
		.pending = CANISTER_QUEUE_INITIALIZER_REL(__memoff(&(__name), \
				&(__name).pending), __can_struct), 	\
		.free = CANISTER_QUEUE_INITIALIZER_REL(__memoff(&(__name), \
				&(__name).free), __can_struct),		\
		.max_items = 0,						\
	}

#define evl_init_tube_rel(__name, __can_struct, __mem, __size)		\
	({								\
		struct __name *__tube = (typeof(__tube))(__mem);	\
		typeof(__tube->free.first[0]) *__i, *__iend;		\
		*__tube = (typeof(*__tube))				\
			TUBE_INITIALIZER_REL(*__tube, __can_struct);	\
		__iend = (typeof(__iend))((char *)__mem + __size);	\
		for (__i = (typeof(__i))(__tube + 1); __i < __iend; __i++) { \
			tube_push_rel(__tube, &(__tube)->free, __i);	\
		}							\
		__tube->max_items = __i - &__tube->free.first[1];	\
		__tube;							\
	})

#define evl_get_tube_size_rel(__name, __count)	\
	evl_get_tube_size(__name, __count)

#define evl_send_tube_rel(__tube, __item)			\
	({							\
		bool __ret = false;				\
		typeof((__tube)->pending.first[0]) *__new;	\
		__new = (typeof(__new))				\
			tube_pull_rel(__tube, &(__tube)->free); \
		if (__new) {					\
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
