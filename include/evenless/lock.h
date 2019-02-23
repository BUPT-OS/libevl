/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_LOCK_H
#define _EVENLESS_LOCK_H

#include <evenless/monitor.h>

struct evl_lock {
	struct evl_monitor __lock;
};

#define EVL_LOCK_INITIALIZER(__name, __clock)  {			\
		.__lock = {						\
			.magic = __MONITOR_UNINIT_MAGIC,		\
			.uninit = {					\
				.type = EVL_MONITOR_PI,			\
				.name = (__name),			\
				.clockfd = (__clock),			\
				.ceiling = 0,				\
			}						\
		}							\
	}

#define EVL_LOCK_CEILING_INITIALIZER(__name, __clock, __ceiling)  {	\
		.__lock = {						\
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

int evl_new_lock(struct evl_lock *lock,
		int clockfd, const char *fmt, ...);

int evl_new_lock_ceiling(struct evl_lock *lock,
			int clockfd, unsigned int ceiling,
			const char *fmt, ...);

int evl_open_lock(struct evl_lock *lock,
		const char *fmt, ...);

int evl_lock(struct evl_lock *lock);

int evl_timedlock(struct evl_lock *lock,
		const struct timespec *timeout);

int evl_trylock(struct evl_lock *lock);

int evl_unlock(struct evl_lock *lock);

int evl_set_lock_ceiling(struct evl_lock *lock,
			unsigned int ceiling);

int evl_get_lock_ceiling(struct evl_lock *lock);

int evl_close_lock(struct evl_lock *lock);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_LOCK_H */
