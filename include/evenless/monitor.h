/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_MONITOR_H
#define _EVENLESS_MONITOR_H

#include <stdbool.h>
#include <time.h>
#include <evenless/atomic.h>
#include <linux/types.h>
#include <uapi/evenless/types.h>
#include <uapi/evenless/monitor.h>

struct evl_thread;

struct evl_monitor {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_monitor_state *state;
			int efd;
			int type;
		} active;
		struct {
			const char *name;
			int clockfd;
			unsigned int ceiling;
			int type;
		} uninit;
	};
};

struct evl_lock {
	struct evl_monitor __lock;
};

struct evl_event {
	struct evl_monitor __event;
};

#define __MONITOR_ACTIVE_MAGIC  0xab12ab12
#define __MONITOR_UNINIT_MAGIC  0xfe11fe11
#define __MONITOR_DEAD_MAGIC	0

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

#endif /* _EVENLESS_MONITOR_H */
