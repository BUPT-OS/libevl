/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_THREAD_H
#define _EVENLESS_THREAD_H

#include <linux/types.h>
#include <limits.h>
#include <evenless/syscall.h>
#include <uapi/evenless/thread.h>
#include <uapi/evenless/sched.h>

/* Enable dlopen() on libevenless.so. */
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

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
struct evl_user_window *evl_current_window;

static inline int evl_get_current_mode(void)
{
	return evl_current_window ?
		evl_current_window->state : T_INBAND;
}

static inline int evl_is_inband(void)
{
	return evl_get_current_mode() & T_INBAND;
}

int evl_attach_self(const char *fmt, ...);

int evl_detach_self(void);
  
int evl_get_self(void);
	
int evl_switch_oob(void);

int evl_switch_inband(void);

int evl_set_schedattr(int efd, const struct evl_sched_attrs *attrs);
  
int evl_get_schedattr(int efd, struct evl_sched_attrs *attrs);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_THREAD_H */
