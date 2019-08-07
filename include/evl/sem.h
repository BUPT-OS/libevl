/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_SEM_H
#define _EVL_SEM_H

#include <time.h>
#include <evl/atomic.h>
#include <linux/types.h>
#include <uapi/evl/types.h>
#include <uapi/evl/monitor.h>

struct evl_sem {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_monitor_state *state;
			int efd;
		} active;
		struct {
			const char *name;
			int clockfd;
			int initval;
		} uninit;
	};
};

#define __SEM_UNINIT_MAGIC	0xed15ed15

#define EVL_SEM_INITIALIZER(__name, __clockfd, __initval)  {	\
		.magic = __SEM_UNINIT_MAGIC,			\
			.uninit = {				\
			.name = (__name),			\
			.clockfd = (__clockfd),			\
			.initval = (__initval),			\
		}						\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_sem(struct evl_sem *sem,
		int clockfd, int initval,
		const char *fmt, ...);

int evl_open_sem(struct evl_sem *sem,
		 const char *fmt, ...);

int evl_close_sem(struct evl_sem *sem);

int evl_get_sem(struct evl_sem *sem);

int evl_timedget_sem(struct evl_sem *sem,
		const struct timespec *timeout);

int evl_put_sem(struct evl_sem *sem);

int evl_tryget_sem(struct evl_sem *sem);

int evl_peek_sem(struct evl_sem *sem,
		int *r_val);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_SEM_H */
