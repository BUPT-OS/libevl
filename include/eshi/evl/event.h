/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_EVENT_H
#define _EVL_ESHI_EVENT_H

#include <time.h>
#include <errno.h>
#include <evl/atomic.h>
#include <evl/mutex.h>

struct evl_event {
	unsigned int magic;
	union {
		struct {
			int fd;
			pthread_cond_t cond;
		} active;
		struct {
			const char *name;
			int clockfd;
			int flags;
		} uninit;
	};
};

#define __EVENT_UNINIT_MAGIC	0x01770177

#define EVL_EVENT_INITIALIZER(__name, __clockfd, __flags)  {	\
		.magic = __EVENT_UNINIT_MAGIC,			\
		.uninit = {					\
			.name = (__name),			\
			.clockfd = (__clockfd),			\
			.flags = (__flags),			\
		}						\
	}

#define evl_new_event(__evt, __fmt, __args...)		\
	evl_create_event(__evt, EVL_CLOCK_MONOTONIC,	\
			0, __fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

int evl_create_event(struct evl_event *evt,
		int clockfd, int flags,
		const char *fmt, ...);

int evl_open_event(struct evl_event *evt,
		const char *fmt, ...);

int evl_wait_event(struct evl_event *evt,
		struct evl_mutex *mutex);

int evl_timedwait_event(struct evl_event *evt,
			struct evl_mutex *mutex,
			const struct timespec *timeout);

int evl_signal_event(struct evl_event *evt);

int evl_broadcast_event(struct evl_event *evt);

int evl_close_event(struct evl_event *evt);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_EVENT_H */
