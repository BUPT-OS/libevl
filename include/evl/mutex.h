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

struct evl_mutex {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_monitor_state *state;
			int efd;
			int type : 2,
			    protocol : 4;
		} active;
		struct {
			const char *name;
			int clockfd;
			unsigned int ceiling;
			int type : 2,
			    protocol : 4;
		} uninit;
	};
};

#define __MUTEX_UNINIT_MAGIC	0xfe11fe11

#define EVL_MUTEX_INITIALIZER(__name, __clock)  {	\
		.magic = __MUTEX_UNINIT_MAGIC,		\
		.uninit = {				\
			.type = EVL_MONITOR_GATE,	\
			.protocol = EVL_GATE_PI,	\
			.name = (__name),		\
			.clockfd = (__clock),		\
			.ceiling = 0,			\
		}					\
	}

#define EVL_MUTEX_CEILING_INITIALIZER(__name, __clock, __ceiling)  {	\
		.magic = __MUTEX_UNINIT_MAGIC,				\
		.uninit = {						\
			.type = EVL_MONITOR_GATE,			\
			.protocol = EVL_GATE_PP,			\
			.name = (__name),				\
			.clockfd = (__clock),				\
			.ceiling = (__ceiling),				\
		}							\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_mutex(struct evl_mutex *mutex,
		int clockfd, const char *fmt, ...);

int evl_new_mutex_ceiling(struct evl_mutex *mutex,
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
