/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _LIB_EVL_X86_SYSCALL_H
#define _LIB_EVL_X86_SYSCALL_H

#include <uapi/asm/evl/syscall.h>

#define evl_syscall3(__nr, __a0, __a1, __a2)				\
	({								\
		register long int _a0 asm ("rdi") = (long)(__a0);	\
		register long int _a1 asm ("rsi") = (long)(__a1);	\
		register long int _a2 asm ("rdx") = (long)(__a2);	\
		long int __res;						\
		__asm__ __volatile__ (					\
			"syscall;\n\t"					\
			: "=a" (__res)					\
			: "0" ((__nr)|__EVL_SYSCALL_BIT),		\
			  "r" (_a0), "r" (_a1), "r" (_a2)		\
			: "cc", "memory", "r11", "cx");			\
		(int)__res;						\
	})

#endif /* !_LIB_EVL_X86_SYSCALL_H */
