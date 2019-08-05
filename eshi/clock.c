/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <time.h>
#include <evl/clock.h>

int evl_set_clock(int clockfd, const struct timespec *tp)
{
	int ret;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
	case EVL_CLOCK_REALTIME:
		ret = clock_settime(-clockfd, tp);
		if (ret)
			return -errno;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int evl_get_clock_resolution(int clockfd, struct timespec *res)
{
	int ret;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
	case EVL_CLOCK_REALTIME:
		ret = clock_getres(-clockfd, res);
		if (ret)
			return -errno;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int evl_sleep(int clockfd, const struct timespec *timeout)
{
	int ret;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
	case EVL_CLOCK_REALTIME:
		ret = clock_nanosleep(-clockfd, TIMER_ABSTIME, timeout, NULL);
		if (ret)
			return -errno;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int evl_udelay(unsigned int usecs)
{
	struct timespec delay;
	int ret;

	delay.tv_sec = usecs / 1000000U;
	delay.tv_nsec = (usecs % 1000000U) * 1000U;
	ret = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	if (ret)
		return -errno;

	return 0;
}
