/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_SEM_H
#define _EVL_ESHI_SEM_H

#include <time.h>
#include <evl/clock.h>

struct evl_sem {
	unsigned int magic;
	union {
		struct {
			clockid_t clock;
			int fd;
		} active;
		struct {
			const char *name;
			int clockfd;
			int initval;
			int flags;
		} uninit;
	};
};

#define __SEM_UNINIT_MAGIC	0xed15ed15

#define EVL_SEM_INITIALIZER(__name, __clockfd, __initval, __flags)  { \
		.magic = __SEM_UNINIT_MAGIC,			      \
		.uninit = {				      	      \
			.name = (__name),			      \
			.clockfd = (__clockfd),			      \
			.initval = (__initval),			      \
			.flags = (__flags),			      \
		}						      \
	}

#define evl_new_sem(__sem, __fmt, __args...)			\
	evl_create_sem(__sem, EVL_CLOCK_MONOTONIC,		\
		0, 0, __fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

int evl_create_sem(struct evl_sem *sem,
		int clockfd, int initval, int flags,
		const char *fmt, ...);

int evl_close_sem(struct evl_sem *sem);

int evl_get_sem(struct evl_sem *sem);

int evl_timedget_sem(struct evl_sem *sem,
		const struct timespec *timeout);

int evl_put_sem(struct evl_sem *sem);

int evl_tryget_sem(struct evl_sem *sem);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_SEM_H */
