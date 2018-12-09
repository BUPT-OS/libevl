/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_ATOMIC_H
#define _EVENLESS_ATOMIC_H

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

#ifndef smp_mb
#define smp_mb()  __sync_synchronize()
#endif

#define compiler_barrier()  __asm__ __volatile__("": : :"memory")

#endif /* _EVENLESS_ATOMIC_H */
