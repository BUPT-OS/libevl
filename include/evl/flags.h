/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_FLAGS_H
#define _EVL_FLAGS_H

#include <time.h>
#include <evl/atomic.h>
#include <linux/types.h>
#include <uapi/evl/types.h>
#include <uapi/evl/monitor.h>
#include <uapi/evl/clock.h>

struct evl_flags {
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

#define __FLAGS_UNINIT_MAGIC	0xfebcfebc

#define EVL_FLAGS_ANY_INITIALIZER(__name, __clockfd, __initval)  {	\
		.magic = __FLAGS_UNINIT_MAGIC,				\
			.uninit = {					\
			.name = (__name),				\
			.clockfd = (__clockfd),				\
			.initval = (__initval),				\
		}							\
	}

#define EVL_FLAGS_INITIALIZER(__name)  {	\
	EVL_FLAGS_ANY_INITIALIZER(__name, EVL_CLOCK_MONOTONIC, 0)

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_flags_any(struct evl_flags *flg,
		int clockfd, int initval,
		const char *fmt, ...);

#define evl_new_flags(__flg, __fmt, __args...)	\
	evl_new_flags_any(__flg, EVL_CLOCK_MONOTONIC, 0, __fmt, ##__args)

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

int evl_peek_flags(struct evl_flags *flg,
		int *r_bits);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_FLAGS_H */
