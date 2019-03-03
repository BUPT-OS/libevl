/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_MUTEX_H
#define _EVENLESS_MUTEX_H

#include <evenless/monitor.h>

struct evl_mutex {
	struct evl_monitor __mutex;
};

#define EVL_MUTEX_INITIALIZER(__name, __clock)  {			\
		.__mutex = {						\
			.magic = __MONITOR_UNINIT_MAGIC,		\
			.uninit = {					\
				.type = EVL_MONITOR_PI,			\
				.name = (__name),			\
				.clockfd = (__clock),			\
				.ceiling = 0,				\
			}						\
		}							\
	}

#define EVL_MUTEX_CEILING_INITIALIZER(__name, __clock, __ceiling)  {	\
		.__mutex = {						\
			.magic = __MONITOR_UNINIT_MAGIC,		\
			.uninit = {					\
				.type = EVL_MONITOR_PP,			\
				.name = (__name),			\
				.clockfd = (__clock),			\
				.ceiling = (__ceiling),			\
			}						\
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

int evl_lock(struct evl_mutex *mutex);

int evl_timedlock(struct evl_mutex *mutex,
		const struct timespec *timeout);

int evl_trylock(struct evl_mutex *mutex);

int evl_unlock(struct evl_mutex *mutex);

int evl_set_mutex_ceiling(struct evl_mutex *mutex,
			unsigned int ceiling);

int evl_get_mutex_ceiling(struct evl_mutex *mutex);

int evl_close_mutex(struct evl_mutex *mutex);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_MUTEX_H */
