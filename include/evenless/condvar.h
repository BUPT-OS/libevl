/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_CONDVAR_H
#define _EVENLESS_CONDVAR_H

#include <evenless/monitor.h>

struct evl_condvar {
	struct evl_monitor __condvar;
};

#define __CONDVAR_UNINIT_MAGIC	0x01770177

#define EVL_CONDVAR_INITIALIZER(__name, __clock)  {		\
		.__condvar = {					\
			.magic = __CONDVAR_UNINIT_MAGIC,	\
			.uninit = {				\
				.type = EVL_MONITOR_EV,		\
				.name = (__name),		\
				.clockfd = (__clock),		\
				.ceiling = 0,			\
			}					\
		}						\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_condvar(struct evl_condvar *condvar,
		int clockfd,
		const char *fmt, ...);

int evl_open_condvar(struct evl_condvar *condvar,
		const char *fmt, ...);

int evl_wait(struct evl_condvar *condvar,
	struct evl_mutex *mutex);

int evl_timedwait(struct evl_condvar *condvar,
		struct evl_mutex *mutex,
		const struct timespec *timeout);

int evl_signal(struct evl_condvar *condvar);

int evl_signal_thread(struct evl_condvar *condvar,
		int thrfd);

int evl_broadcast(struct evl_condvar *condvar);

int evl_close_condvar(struct evl_condvar *condvar);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_CONDVAR_H */
