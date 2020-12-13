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

int evl_attach_thread(int flags, const char *fmt, ...);

int evl_detach_thread(int flags);

int evl_detach_self(void);

int evl_get_self(void);

int evl_switch_oob(void);

int evl_switch_inband(void);

bool evl_is_inband(void);

int evl_get_state(int efd, struct evl_thread_state *statebuf);

int evl_unblock_thread(int efd);

int evl_demote_thread(int efd);

int evl_set_thread_mode(int efd, int mask,
			int *oldmask);

int evl_clear_thread_mode(int efd, int mask,
			int *oldmask);

int evl_subscribe(int ofd,
		unsigned int backlog_count,
		int flags);

int evl_unsubscribe(int ofd);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_THREAD_H */
