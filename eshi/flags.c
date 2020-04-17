/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <evl/flags.h>
#include <sys/eventfd.h>
#include "internal.h"

#define __FLAGS_ACTIVE_MAGIC	0xb42bb42b
#define __FLAGS_DEAD_MAGIC	0

int evl_create_flags(struct evl_flags *flg, int clockfd,
		int initval, int flags,
		const char *fmt, ...)
{
	int fd;

	if (!eshi_is_initialized())
		return -ENXIO;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
		flg->active.clock = CLOCK_MONOTONIC;
		break;
	case EVL_CLOCK_REALTIME:
		flg->active.clock = CLOCK_REALTIME;
		break;
	default:
		return -EINVAL;
	}

	fd = eventfd(initval, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fd < 0)
		return -errno;

	flg->active.fd = fd;
	flg->magic = __FLAGS_ACTIVE_MAGIC;

	return fd;
}

int evl_close_flags(struct evl_flags *flg)
{
	if (flg->magic == __FLAGS_UNINIT_MAGIC)
		return 0;

	if (flg->magic != __FLAGS_ACTIVE_MAGIC)
		return -EINVAL;

	close(flg->active.fd);
	flg->magic = __FLAGS_DEAD_MAGIC;

	return 0;
}

static int check_sanity(struct evl_flags *flg)
{
	int efd;

	if (flg->magic == __FLAGS_UNINIT_MAGIC) {
		efd = evl_create_flags(flg,
				flg->uninit.clockfd,
				flg->uninit.initval,
				flg->uninit.flags,
				flg->uninit.name);
		return efd < 0 ? efd : 0;
	}

	return flg->magic != __FLAGS_ACTIVE_MAGIC ? -EINVAL : 0;
}

static int timedwait_flags(struct evl_flags *flg,
			const struct timespec *timeout,
			int *r_bits)
{
	const struct timespec *tp = timeout;
	struct timespec ts, now;
	struct pollfd pollfd;
	uint64_t val;
	int ret;

	if (!tp || (tp->tv_sec == 0 && tp->tv_nsec == 0))
		goto poll;

	for (;;) {
		if (tp) {
			clock_gettime(flg->active.clock, &now);
			ts = *timeout;
			timespec_sub(&ts, &now);
			if (ts.tv_sec < 0) {
				ts.tv_sec = 0;
				ts.tv_nsec = 0;
			}
			tp = &ts;
		}
	poll:
		pollfd.fd = flg->active.fd;
		pollfd.events = POLLIN;
		pollfd.revents = 0;
		ret = ppoll(&pollfd, 1, tp, NULL);
		if (ret < 0)
			return -errno;

		if (ret == 0)
			break;

		if (!(pollfd.revents & POLLIN))
			return -EINVAL;

		ret = read(flg->active.fd, &val, sizeof(val));
		if (ret > 0) {
			*r_bits = (unsigned int)(val & ~0);
			return 0;
		}

		if (errno != -EAGAIN)
			return -errno;
	}

	return -ETIMEDOUT;
}

int evl_timedwait_flags(struct evl_flags *flg,
			const struct timespec *timeout,
			int *r_bits)
{
	int ret;

	if (timeout == NULL)
		return -EINVAL;

	if (timeout->tv_sec < 0 || timeout->tv_nsec >= 1000000000L)
		return -EINVAL;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	return timedwait_flags(flg, timeout, r_bits);
}

int evl_wait_flags(struct evl_flags *flg, int *r_bits)
{
	int ret;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	return timedwait_flags(flg, NULL, r_bits);
}

int evl_trywait_flags(struct evl_flags *flg, int *r_bits)
{
	struct timespec zerotime = { .tv_sec = 0, .tv_nsec = 0};
	int ret;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	ret = timedwait_flags(flg, &zerotime, r_bits);
	if (ret == -ETIMEDOUT)
		return -EAGAIN;

	return ret;
}

int evl_post_flags(struct evl_flags *flg, int bits)
{
	uint64_t val = (uint64_t)(unsigned int)bits;
	int ret;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	ret = write(flg->active.fd, &val, sizeof(val));
	if (ret != sizeof(val))
		return -errno;

	return 0;
}
