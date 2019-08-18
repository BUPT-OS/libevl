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

#define EVL_MUTEX_NORMAL     0
#define EVL_MUTEX_RECURSIVE  1

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
			int monitor : 2,
			    protocol : 4,
			    type : 1;
		} uninit;
	};
};

#define EVL_MUTEX_INITIALIZER(__type, __name, __clockfd)  {	\
		.magic = __MUTEX_UNINIT_MAGIC,			\
			.uninit = {				\
			.monitor = EVL_MONITOR_GATE,		\
			.protocol = EVL_GATE_PI,		\
			.type = (__type),			\
			.name = (__name),			\
			.clockfd = (__clockfd),			\
			.ceiling = 0,				\
		}						\
	}

#define EVL_MUTEX_CEILING_INITIALIZER(__type, __name, __clockfd, __ceiling)  { \
		.magic = __MUTEX_UNINIT_MAGIC,				\
			.uninit = {					\
			.monitor = EVL_MONITOR_GATE,			\
			.protocol = EVL_GATE_PP,			\
			.type = (__type),				\
			.name = (__name),				\
			.clockfd = (__clockfd),				\
			.ceiling = (__ceiling),				\
		}							\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_mutex(struct evl_mutex *mutex, int type,
		int clockfd, const char *fmt, ...);

int evl_new_mutex_ceiling(struct evl_mutex *mutex, int type,
			int clockfd, unsigned int ceiling,
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
