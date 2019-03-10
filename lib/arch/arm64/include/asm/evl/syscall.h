/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _LIB_EVL_ARM64_SYSCALL_H
#define _LIB_EVL_ARM64_SYSCALL_H

#include <uapi/asm/evl/syscall.h>

#define evl_syscall3(__nr, __a0, __a1, __a2)			\
	({								\
		register unsigned int  __sc  __asm__("w8");		\
		register unsigned long __res __asm__("x0");		\
		register unsigned long __x0  __asm__("x0");		\
		register unsigned long __x1  __asm__("x1");		\
		register unsigned long __x2  __asm__("x2");		\
		__sc = (unsigned int)((__nr)|__EVL_SYSCALL_BIT);	\
		__x0 = (unsigned long)(__a0);				\
		__x1 = (unsigned long)(__a1);				\
		__x2 = (unsigned long)(__a2);				\
		__asm__ __volatile__ (					\
			"svc 0;\n\t"					\
			: "=r" (__res)					\
			: "r" (__sc),					\
			  "r" (__x0), "r" (__x1), "r" (__x2)		\
			: "cc", "memory");				\
		__res;							\
	})

#endif /* !_LIB_EVL_ARM64_SYSCALL_H */
