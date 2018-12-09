/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/types.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <evenless/clock.h>
#include <evenless/thread.h>
#include <uapi/evenless/clock.h>

#define do_call(__efd, __args...)				\
	({							\
		int __ret;					\
		if (evl_is_inband())				\
			__ret = ioctl(__efd, ##__args);		\
		else						\
			__ret = oob_ioctl(__efd, ##__args);	\
		__ret ? -errno : 0;				\
	})

int evl_set_clock(int efd, const struct timespec *tp)
{
	return do_call(efd, EVL_CLKIOC_SET_TIME, tp);
}

int evl_get_clock_resolution(int efd, struct timespec *tp)
{
	return do_call(efd, EVL_CLKIOC_GET_RES, tp);
}

int evl_adjust_clock(int efd, struct timex *tx)
{
	return do_call(efd, EVL_CLKIOC_ADJ_TIME, tx);
}

int evl_delay(int efd, const struct timespec *timeout,
	      struct timespec *remain)
{
	struct evl_clock_delayreq req = {
		.timeout = *timeout,
		.remain = remain,
	};

	return oob_ioctl(efd, EVL_CLKIOC_DELAY, &req) ? -errno : 0;
}
