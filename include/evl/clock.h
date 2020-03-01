/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_CLOCK_H
#define _EVL_CLOCK_H

#include <time.h>
#include <errno.h>
#include <sys/timex.h>
#include <sys/ioctl.h>
#include <evl/syscall.h>
#include <uapi/evl/clock.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline
int evl_read_clock(int clockfd, struct timespec *tp)
{
	extern int (*arch_clock_gettime)(clockid_t clk_id,
					 struct timespec *tp);
	switch (clockfd) {
	case -CLOCK_MONOTONIC:
	case -CLOCK_REALTIME:
		return arch_clock_gettime(-clockfd, tp) ? -errno : 0;
	default:
		return oob_ioctl(clockfd, EVL_CLKIOC_GET_TIME, tp) ? -errno : 0;
	}
}

int evl_set_clock(int clockfd, const struct timespec *tp);

int evl_get_clock_resolution(int clockfd, struct timespec *tp);

int evl_sleep_until(int clockfd, const struct timespec *timeout);

int evl_usleep(useconds_t usecs);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_CLOCK_H */
