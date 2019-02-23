/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_EVENT_H
#define _EVENLESS_EVENT_H

#include <evenless/monitor.h>

struct evl_event {
	struct evl_monitor __event;
};

#define EVL_EVENT_INITIALIZER(__name, __clock)  {			\
		.__event = {						\
			.magic = __MONITOR_UNINIT_MAGIC,		\
			.uninit = {					\
				.type = EVL_MONITOR_EV,			\
				.name = (__name),			\
				.clockfd = (__clock),			\
				.ceiling = 0,				\
			}						\
		}							\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_event(struct evl_event *event,
		   int clockfd,
		   const char *fmt, ...);

int evl_open_event(struct evl_event *event,
		const char *fmt, ...);

int evl_wait(struct evl_event *event,
	struct evl_lock *lock);

int evl_timedwait(struct evl_event *event,
		struct evl_lock *lock,
		const struct timespec *timeout);

int evl_signal(struct evl_event *event);

int evl_signal_thread(struct evl_event *event,
		int thrfd);

int evl_broadcast(struct evl_event *event);

int evl_close_event(struct evl_event *event);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_EVENT_H */
