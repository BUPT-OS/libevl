/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_CLOCK_H
#define _EVL_ESHI_CLOCK_H

#include <time.h>
#include <errno.h>
#include <evl/syscall.h>

#define EVL_CLOCK_MONOTONIC  (-CLOCK_MONOTONIC)
#define EVL_CLOCK_REALTIME   (-CLOCK_REALTIME)

#ifdef __cplusplus
extern "C" {
#endif

static inline
int evl_read_clock(int clockfd, struct timespec *tp)
{
	int ret;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
	case EVL_CLOCK_REALTIME:
		break;
	default:
		return -EINVAL;
	}

	ret = clock_gettime(-clockfd, tp);
	if (ret)
		return -errno;

	return 0;
}

int evl_set_clock(int clockfd, const struct timespec *tp);

int evl_get_clock_resolution(int clockfd, struct timespec *tp);

int evl_sleep_until(int clockfd, const struct timespec *timeout);

int evl_usleep(useconds_t usecs);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_CLOCK_H */
