/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_FLAGS_H
#define _EVL_ESHI_FLAGS_H

#include <time.h>
#include <evl/clock.h>

struct evl_flags {
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
		} uninit;
	};
};

#define __FLAGS_UNINIT_MAGIC	0xfebcfebc

#define EVL_FLAGS_INITIALIZER(__name, __clockfd, __initval)  {		\
		.magic = __FLAGS_UNINIT_MAGIC,				\
			.uninit = {					\
			.name = (__name),				\
			.clockfd = (__clockfd),				\
			.initval = (__initval),				\
		}							\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_flags(struct evl_flags *flg,
		int clockfd, int initval,
		const char *fmt, ...);

int evl_open_flags(struct evl_flags *flg,
		const char *fmt, ...);

int evl_close_flags(struct evl_flags *flg);

int evl_wait_flags(struct evl_flags *flg,
		int *r_bits);

int evl_timedwait_flags(struct evl_flags *flg,
			const struct timespec *timeout,
			int *r_bits);

int evl_post_flags(struct evl_flags *flg,
		int bits);

int evl_trywait_flags(struct evl_flags *flg,
		int *r_bits);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_FLAGS_H */
