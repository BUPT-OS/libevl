/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_MUTEX_H
#define _EVL_MUTEX_H

#include <time.h>
#include <linux/types.h>
#include <evl/atomic.h>
#include <uapi/evl/types.h>
#include <uapi/evl/monitor.h>
#include <uapi/evl/factory.h>

#define EVL_MUTEX_NORMAL     (0 << 0)
#define EVL_MUTEX_RECURSIVE  (1 << 0)

#define __MUTEX_UNINIT_MAGIC	0xfe11fe11
#define __MUTEX_ACTIVE_MAGIC	0xab12ab12

struct evl_mutex {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_monitor_state *state;
			int efd;
			int monitor : 2,
			    protocol : 4;
		} active;
		struct {
			const char *name;
			int clockfd;
			unsigned int ceiling;
			int flags;
			int monitor : 2;
		} uninit;
	} u;
};

#define EVL_MUTEX_INITIALIZER(__name, __clockfd, __ceiling, __flags)  { \
		.magic = __MUTEX_UNINIT_MAGIC,				\
		.u = {							\
			.uninit = {					\
				.name = (__name),			\
				.clockfd = (__clockfd),			\
				.ceiling = (__ceiling),			\
				.flags = (__flags),			\
				.monitor = EVL_MONITOR_GATE,		\
			}						\
		}							\
	}

#define evl_new_mutex(__mutex, __fmt, __args...)		\
	evl_create_mutex(__mutex, EVL_CLOCK_MONOTONIC,		\
			0, EVL_MUTEX_NORMAL|EVL_CLONE_PRIVATE,	\
			__fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

int evl_create_mutex(struct evl_mutex *mutex,
		int clockfd, unsigned int ceiling, int flags,
		const char *fmt, ...);

int evl_open_mutex(struct evl_mutex *mutex,
		const char *fmt, ...);

int evl_lock_mutex(struct evl_mutex *mutex);

int evl_timedlock_mutex(struct evl_mutex *mutex,
			const struct timespec *timeout);

int evl_trylock_mutex(struct evl_mutex *mutex);

int evl_unlock_mutex(struct evl_mutex *mutex);

int evl_set_mutex_ceiling(struct evl_mutex *mutex,
			unsigned int ceiling);

int evl_get_mutex_ceiling(struct evl_mutex *mutex);

int evl_close_mutex(struct evl_mutex *mutex);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_MUTEX_H */
