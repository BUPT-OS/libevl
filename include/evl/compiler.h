/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_COMPILER_H
#define _EVL_COMPILER_H

#include <stddef.h>
#include <limits.h>

#ifndef container_of
#define container_of(ptr, type, member)					\
	({								\
		const __typeof__(((type *)0)->member) *__mptr = (ptr);	\
		(type *)((char *)__mptr - offsetof(type, member));	\
	})
#endif

#ifndef __stringify
#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)
#endif

#ifndef likely
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#endif

#ifndef __noreturn
#define __noreturn	__attribute__((__noreturn__))
#endif

#ifndef __must_check
#define __must_check	__attribute__((__warn_unused_result__))
#endif

#ifndef __weak
#define __weak		__attribute__((__weak__))
#endif

#ifndef __maybe_unused
#define __maybe_unused	__attribute__((__unused__))
#endif

#ifndef __aligned
#define __aligned(__n)	__attribute__((aligned (__n)))
#endif

#ifndef __deprecated
#define __deprecated	__attribute__((__deprecated__))
#endif

#ifndef __packed
#define __packed	__attribute__((__packed__))
#endif

#ifndef __alloc_size
#define __alloc_size(__args)	__attribute__((__alloc_size__(__args)))
#endif

#ifndef __align_to
#define __align_to(__size, __al)  (((__size) + (__al) - 1) & (~((__al) - 1)))
#endif

#define __lzcount(__x)					\
	((__x) == 0 ? (int)(sizeof(__x) * __CHAR_BIT__)	\
		: sizeof(__x) <= sizeof(int) ?		\
		__builtin_clz((int)(__x)) +		\
		(int)(sizeof(int) - sizeof(__x))	\
		: sizeof(__x) <= sizeof(long) ?		\
		__builtin_clzl((long)__x)		\
		: __builtin_clzll(__x))

#define __tzcount(__x)					\
	((__x) == 0 ? (int)(sizeof(__x) * __CHAR_BIT__)	\
		: sizeof(__x) <= sizeof(int) ?		\
		__builtin_ctz((int)(__x))		\
		: sizeof(__x) <= sizeof(long) ?		\
		__builtin_ctzl((long)__x)		\
		: __builtin_ctzll(__x))

#define __force_read_access(__var)					\
	do {								\
		__typeof(__var) __v = *((volatile typeof(__var) *)&(__var)); \
		(void)__v;						\
	} while (0)

#endif /* _EVL_COMPILER_H */
