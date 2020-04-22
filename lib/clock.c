/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/types.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <evl/clock.h>
#include <evl/thread.h>
#include <uapi/evl/clock.h>
#include "internal.h"

int evl_mono_clockfd = -ENXIO,
	evl_real_clockfd = -ENXIO;

int evl_set_clock(int clockfd, const struct timespec *tp)
{
	struct __evl_timespec kts;
	int ret;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
	case EVL_CLOCK_REALTIME:
		ret = clock_settime(-clockfd, tp);
		if (ret)
			return -errno;
		break;
	default:
		ret = __evl_common_ioctl(clockfd, EVL_CLKIOC_SET_TIME,
					__evl_ktimespec(tp, kts));
	}

	return ret;
}

int evl_get_clock_resolution(int clockfd, struct timespec *tp)
{
	int ret;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
	case EVL_CLOCK_REALTIME:
		ret = clock_getres(-clockfd, tp);
		if (ret)
			return -errno;
		break;
	default:
		ret = __evl_common_ioctl(clockfd, EVL_CLKIOC_GET_RES, tp);
	}

	return ret;
}

int evl_sleep_until(int clockfd, const struct timespec *timeout)
{
	struct __evl_timespec kts;

	if (clockfd == EVL_CLOCK_MONOTONIC)
		clockfd = evl_mono_clockfd;
	else if (clockfd == EVL_CLOCK_REALTIME)
		clockfd = evl_real_clockfd;

	return oob_ioctl(clockfd, EVL_CLKIOC_SLEEP,
			__evl_ktimespec(timeout, kts)) ? -errno : 0;
}

static void timespec_add_ns(struct timespec *__restrict r,
			const struct timespec *__restrict t,
			unsigned int usecs)
{
	long s, rem;

	s = usecs / 1000000000;
	rem = usecs - s * 1000000000;
	r->tv_sec = t->tv_sec + s;
	r->tv_nsec = t->tv_nsec + rem;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}

int evl_usleep(useconds_t usecs)
{
	struct timespec now, next;

	if (usecs < 1 || usecs > 1000000)
		return -EINVAL;

	evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
	timespec_add_ns(&next, &now, usecs * 1000);

	return evl_sleep_until(evl_mono_clockfd, &next);
}

int attach_evl_clocks(void)
{
	struct timespec dummy;

	evl_mono_clockfd = open_evl_element(EVL_CLOCK_DEV,
					    EVL_CLOCK_MONOTONIC_DEV);
	if (evl_mono_clockfd < 0)
		return evl_mono_clockfd;

	evl_real_clockfd = open_evl_element(EVL_CLOCK_DEV,
					    EVL_CLOCK_REALTIME_DEV);
	if (evl_real_clockfd < 0) {
		close(evl_mono_clockfd);
		return evl_real_clockfd;
	}

	/*
	 * With some architectures, the vDSO might have to mmap the
	 * clock source register(s) into the caller's address space
	 * upon first read call (ARM), which would trigger an in-band
	 * syscall from the vDSO code. Force a dummy read of the
	 * monotonic clock to map such register(s) now, so that we
	 * won't receive SIGDEBUG due to switching in-band
	 * inadvertently for this reason later on.
	 */
	evl_read_clock(EVL_CLOCK_MONOTONIC, &dummy);

	return 0;
}
