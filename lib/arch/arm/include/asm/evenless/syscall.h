/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _LIB_EVENLESS_ARM_SYSCALL_H
#define _LIB_EVENLESS_ARM_SYSCALL_H

#include <uapi/asm/dovetail.h>

#define evenless_syscall3(__nr, __a0, __a1, __a2)		\
	({							\
		register unsigned long __asc __asm__("r7");	\
		register unsigned long __res __asm__("r0");	\
		register unsigned long __sc  __asm__("r0");	\
		register unsigned long __r1  __asm__("r1");	\
		register unsigned long __r2  __asm__("r2");	\
		register unsigned long __r3  __asm__("r3");	\
		__asc = __ARM_NR_dovetail;			\
		__sc = (unsigned int)(__nr);			\
		__r1 = (unsigned long)(__a0);			\
		__r2 = (unsigned long)(__a1);			\
		__r3 = (unsigned long)(__a2);			\
		__asm__ __volatile__ (				\
			"swi #0;\n\t"				\
			: "=r" (__res)				\
			: "r" (__asc), "r" (__sc), 		\
			  "r" (__r1), "r" (__r2), "r" (__r3) 	\
			: "cc", "memory");			\
		__res;						\
	})

#endif /* !_LIB_EVENLESS_ARM_SYSCALL_H */
