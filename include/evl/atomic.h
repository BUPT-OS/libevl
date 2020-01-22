/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ATOMIC_H
#define _EVL_ATOMIC_H

#include <linux/types.h>

typedef struct { __s32 val; } atomic_t;

#define ATOMIC_INIT(__val) { (__val) }

/* For scalar types only! */
#define atomic_store(__ptr, __val)				\
	do {							\
		(*(volatile typeof(__ptr))(__ptr)) = (__val);	\
	} while (0)

#define atomic_load(__ptr)					\
	({							\
		(*(volatile typeof(__ptr))(__ptr));		\
	})

static inline int atomic_read(const atomic_t *ptr)
{
	return atomic_load(&ptr->val);
}

static inline void atomic_set(atomic_t *ptr, int val)
{
	atomic_store(&ptr->val, val);
}

#ifndef atomic_cmpxchg
#define atomic_cmpxchg(__ptr, __old, __new)  \
	__sync_val_compare_and_swap(&(__ptr)->val, __old, __new)
#endif

#ifndef atomic_add_return
#define atomic_add_return(__ptr, __n)	\
	__sync_add_and_fetch(&(__ptr)->val, __n)
#endif

#ifndef smp_mb
#define smp_mb()  __sync_synchronize()
#endif

#endif /* _EVL_ATOMIC_H */
