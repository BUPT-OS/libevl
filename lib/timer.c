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

#define do_call(__timerfd, __args...)				\
	({							\
		int __ret;					\
		if (evl_is_inband())				\
			__ret = ioctl(__timerfd, ##__args);	\
		else						\
			__ret = oob_ioctl(__timerfd, ##__args);	\
		__ret ? -errno : 0;				\
	})

int evl_new_timer(int clockfd)
{
	int ret, efd;

	if (evl_mono_clockfd < 0)
		return -ENXIO;

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
	struct __evl_itimerspec kits, koits;
	struct evl_timerfd_setreq sreq;

	sreq.value = __evl_kitimerspec(value, kits);
	sreq.ovalue = __evl_kitimerspec(ovalue, koits);

	return do_call(efd, EVL_TFDIOC_SET, &sreq);
}

int evl_get_timer(int efd, struct itimerspec *value)
{
	struct __evl_itimerspec kits;

	return do_call(efd, EVL_TFDIOC_GET, __evl_kitimerspec(value, kits));
}
