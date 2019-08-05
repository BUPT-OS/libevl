/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_THREAD_H
#define _EVL_ESHI_THREAD_H

#include <limits.h>
#include <errno.h>
#include <stdbool.h>
#include <evl/syscall.h>

/* Enable dlopen() on libeshi.so. */
#define EVL_TLS_MODEL	"global-dynamic"

#define EVL_STACK_DEFAULT			\
	({					\
		int __ret = PTHREAD_STACK_MIN;	\
		if (__ret < 65536)		\
			__ret = 65536;		\
		__ret;				\
	})

#ifdef __cplusplus
extern "C" {
#endif

static inline bool evl_is_inband(void)
{
	return true;
}

static inline int evl_switch_oob(void)
{
	return -ENOSYS;
}

static inline int evl_switch_inband(void)
{
	return 0;
}

int evl_attach_self(const char *fmt, ...);

int evl_detach_self(void);

int evl_get_self(void);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_THREAD_H */
