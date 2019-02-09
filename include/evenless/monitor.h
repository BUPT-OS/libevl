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

#define __MONITOR_ACTIVE_MAGIC  0xab12ab12
#define __MONITOR_UNINIT_MAGIC  0xfe11fe11
#define __MONITOR_DEAD_MAGIC	0

#define EVL_GATE_INITIALIZER(__name, __clock, __ceiling)  {		\
		.magic = __MONITOR_UNINIT_MAGIC,			\
		.uninit = {						\
			.type = (__ceiling) ? EVL_MONITOR_PP : EVL_MONITOR_PI, \
			.name = (__name),				\
			.clockfd = (__clockfd),				\
			.ceiling = (__ceiling),				\
		}							\
	}

#define EVL_EVENT_INITIALIZER(__name, __clockfd)  {			\
		.magic = __MONITOR_UNINIT_MAGIC,			\
		.uninit = {						\
			.type = EVL_MONITOR_EV,				\
			.name = (__name),				\
			.clockfd = (__clockfd),				\
		}							\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_gate(struct evl_monitor *gate,
		 int type, int clockfd,
		 unsigned int ceiling,
		 const char *fmt, ...);

int evl_new_event(struct evl_monitor *event,
		   int clockfd,
		   const char *fmt, ...);

int evl_open_monitor(struct evl_monitor *mon,
		     const char *fmt, ...);

int evl_release_monitor(struct evl_monitor *mon);

int evl_enter_gate(struct evl_monitor *gate);

int evl_enter_gate_timed(struct evl_monitor *gate,
			const struct timespec *timeout);

int evl_tryenter_gate(struct evl_monitor *gate);

int evl_exit_gate(struct evl_monitor *gate);

int evl_set_gate_ceiling(struct evl_monitor *gate,
			 unsigned int ceiling);

int evl_get_gate_ceiling(struct evl_monitor *gate);

int evl_wait_event(struct evl_monitor *event,
		struct evl_monitor *gate);

int evl_wait_event_timed(struct evl_monitor *event,
			struct evl_monitor *gate,
			const struct timespec *timeout);

int evl_signal_event(struct evl_monitor *event);

int evl_signal_event_targeted(struct evl_monitor *event,
			      int thrfd);

int evl_broadcast_event(struct evl_monitor *event);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_MONITOR_H */
