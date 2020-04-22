/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_THREAD_H
#define _EVL_THREAD_H

#include <linux/types.h>
#include <limits.h>
#include <stdbool.h>
#include <sched.h>
#include <evl/syscall.h>
#include <uapi/evl/signal.h>
#include <uapi/evl/thread.h>
#include <uapi/evl/sched.h>
#include <uapi/evl/factory.h>

/* Enable dlopen() on libevl.so. */
#define EVL_TLS_MODEL	"global-dynamic"

#define EVL_STACK_DEFAULT			\
	({					\
		int __ret = PTHREAD_STACK_MIN;	\
		if (__ret < 65536)		\
			__ret = 65536;		\
		__ret;				\
	})

#define evl_attach_self(__fmt, __args...)	\
	evl_attach_thread(EVL_CLONE_PRIVATE, __fmt, ##__args)

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

static inline bool evl_is_inband(void)
{
	return !!(evl_get_current_mode() & T_INBAND);
}

int evl_attach_thread(int flags, const char *fmt, ...);

int evl_detach_thread(int flags);

static inline int evl_detach_self(void)
{
	return evl_detach_thread(0);
}

int evl_get_self(void);

int evl_switch_oob(void);

int evl_switch_inband(void);

int evl_get_state(int efd, struct evl_thread_state *statebuf);

int evl_unblock_thread(int efd);

int evl_demote_thread(int efd);

int evl_set_thread_mode(int efd, int mask,
			int *oldmask);

int evl_clear_thread_mode(int efd, int mask,
			int *oldmask);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_THREAD_H */
