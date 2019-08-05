/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <time.h>
#include <sys/timerfd.h>
#include <evl/clock.h>
#include <evl/timer.h>

int evl_new_timer(int clockfd)
{
	clockid_t clk;
	int fd;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
		clk = CLOCK_MONOTONIC;
		break;
	case EVL_CLOCK_REALTIME:
		clk = CLOCK_REALTIME;
		break;
	default:
		return -EINVAL;
	}

	fd = timerfd_create(clk, TFD_CLOEXEC);
	if (fd < 0)
		return -errno;

	return fd;
}

int evl_set_timer(int efd,
		struct itimerspec *value,
		struct itimerspec *ovalue)
{
	int ret;

	ret = timerfd_settime(efd, TFD_TIMER_ABSTIME, value, ovalue);
	if (ret)
		return -errno;

	return 0;
}

int evl_get_timer(int efd, struct itimerspec *value)
{
	int ret;

	ret = timerfd_gettime(efd, value);
	if (ret)
		return -errno;

	return 0;
}
