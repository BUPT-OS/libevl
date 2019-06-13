/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ATOMIC_H
#define _EVL_ATOMIC_H

#include <linux/types.h>

typedef struct { __u32 val; } atomic_t;

#define ATOMIC_INIT(__val) { (__val) }

static inline int atomic_read(const atomic_t *ptr)
{
	return ptr->val;
}

static inline void atomic_set(atomic_t *ptr, long val)
{
	ptr->val = val;
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

#define compiler_barrier()  __asm__ __volatile__("": : :"memory")

#endif /* _EVL_ATOMIC_H */
