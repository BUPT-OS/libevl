/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_MUTEX_H
#define _EVL_ESHI_MUTEX_H

#include <time.h>
#include <pthread.h>
#include <evl/clock.h>

struct evl_mutex {
	unsigned int magic;
	union {
		struct {
			pthread_mutex_t mutex;
			clockid_t clock;
			int fd;
		} active;
		struct {
			const char *name;
			int clockfd;
			unsigned int ceiling;
		} uninit;
	};
};

#define __MUTEX_ACTIVE_MAGIC	0xab12ab12
#define __MUTEX_UNINIT_MAGIC	0xfe11fe11

#define EVL_MUTEX_INITIALIZER(__name, __clockfd)  {	\
		.magic = __MUTEX_UNINIT_MAGIC,		\
		.uninit = {				\
			.name = (__name),		\
			.clockfd = (__clockfd),		\
			.ceiling = 0,			\
		}					\
	}

#define EVL_MUTEX_CEILING_INITIALIZER(__name, __clockfd, __ceiling)  {	\
		.magic = __MUTEX_UNINIT_MAGIC,				\
		.uninit = {						\
			.name = (__name),				\
			.clockfd = (__clockfd),				\
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

#endif /* _EVL_ESHI_MUTEX_H */
