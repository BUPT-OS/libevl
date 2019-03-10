/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <evl/syscall.h>
#include <evl/timer.h>
#include "internal.h"

int evl_new_timer(int clockfd)
{
	int ret, efd;

	if (clockfd == EVL_CLOCK_MONOTONIC)
		clockfd = evl_mono_clockfd;
	else if (clockfd == EVL_CLOCK_REALTIME)
		clockfd = evl_real_clockfd;

	ret = ioctl(clockfd, EVL_CLKIOC_NEW_TIMER, &efd);
	if (ret)
		return -errno;

	return efd;
}

int evl_set_timer(int efd,
		struct itimerspec *value,
		struct itimerspec *ovalue)
{
	struct evl_timerfd_setreq sreq;
	int ret;

	sreq.value = value;
	sreq.ovalue = ovalue;
	ret = oob_ioctl(efd, EVL_TFDIOC_SET, &sreq);

	return ret ? -errno : 0;
}

int evl_get_timer(int efd, struct itimerspec *value)
{
	struct evl_timerfd_getreq greq;
	int ret;

	greq.value = value;
	ret = oob_ioctl(efd, EVL_TFDIOC_GET, &greq);

	return ret ? -errno : 0;
}
