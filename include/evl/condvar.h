/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_CONDVAR_H
#define _EVL_CONDVAR_H

#include <time.h>
#include <linux/types.h>
#include <evl/atomic.h>
#include <uapi/evl/types.h>
#include <uapi/evl/monitor.h>

struct evl_condvar {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_monitor_state *state;
			int efd;
		} active;
		struct {
			const char *name;
			int clockfd;
		} uninit;
	};
};

#define __CONDVAR_UNINIT_MAGIC	0x01770177

#define EVL_CONDVAR_INITIALIZER(__name, __clock)  {	\
		.magic = __CONDVAR_UNINIT_MAGIC,	\
		.uninit = {				\
			.name = (__name),		\
			.clockfd = (__clock),		\
		}					\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_condvar(struct evl_condvar *cv,
		int clockfd,
		const char *fmt, ...);

int evl_open_condvar(struct evl_condvar *cv,
		const char *fmt, ...);

int evl_wait(struct evl_condvar *cv,
	struct evl_mutex *mutex);

int evl_timedwait(struct evl_condvar *cv,
		struct evl_mutex *mutex,
		const struct timespec *timeout);

int evl_signal(struct evl_condvar *cv);

int evl_signal_thread(struct evl_condvar *cv,
		int thrfd);

int evl_broadcast(struct evl_condvar *cv);

int evl_close_condvar(struct evl_condvar *cv);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_CONDVAR_H */
