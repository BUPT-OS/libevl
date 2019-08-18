/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <evl/sem.h>
#include <sys/eventfd.h>
#include "internal.h"

#define __SEM_ACTIVE_MAGIC	0xcb13cb13
#define __SEM_DEAD_MAGIC	0

int evl_new_sem_any(struct evl_sem *sem, int clockfd, int initval,
		const char *fmt, ...)
{
	int fd;

	/*
	 *  evl_init() must have run: exclusively for proper emulation
	 *  of libevl.
	 */
	if (!eshi_is_initialized())
		return -ENXIO;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
		sem->active.clock = CLOCK_MONOTONIC;
		break;
	case EVL_CLOCK_REALTIME:
		sem->active.clock = CLOCK_REALTIME;
		break;
	default:
		return -EINVAL;
	}

	fd = eventfd(initval, EFD_SEMAPHORE | EFD_NONBLOCK | EFD_CLOEXEC);
	if (fd < 0)
		return -errno;

	sem->active.fd = fd;
	sem->magic = __SEM_ACTIVE_MAGIC;

	return fd;
}

int evl_close_sem(struct evl_sem *sem)
{
	if (sem->magic == __SEM_UNINIT_MAGIC)
		return 0;

	if (sem->magic != __SEM_ACTIVE_MAGIC)
		return -EINVAL;

	close(sem->active.fd);
	sem->magic = __SEM_DEAD_MAGIC;

	return 0;
}

static int check_sanity(struct evl_sem *sem)
{
	int efd;

	if (sem->magic == __SEM_UNINIT_MAGIC) {
		efd = evl_new_sem_any(sem,
				sem->uninit.clockfd,
				sem->uninit.initval,
				sem->uninit.name);
		return efd < 0 ? efd : 0;
	}

	return sem->magic != __SEM_ACTIVE_MAGIC ? -EINVAL : 0;
}

static int timedget_sem(struct evl_sem *sem,
			const struct timespec *timeout)
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
			clock_gettime(sem->active.clock, &now);
			ts = *timeout;
			timespec_sub(&ts, &now);
			if (ts.tv_sec < 0) {
				ts.tv_sec = 0;
				ts.tv_nsec = 0;
			}
			tp = &ts;
		}
	poll:
		pollfd.fd = sem->active.fd;
		pollfd.events = POLLIN;
		pollfd.revents = 0;
		ret = ppoll(&pollfd, 1, tp, NULL);
		if (ret < 0)
			return -errno;

		if (ret == 0)
			break;

		if (!(pollfd.revents & POLLIN))
			return -EINVAL;

		ret = read(sem->active.fd, &val, sizeof(val));
		if (ret > 0)
			return 0;

		if (errno != -EAGAIN)
			return -errno;
	}

	return -ETIMEDOUT;
}

int evl_timedget_sem(struct evl_sem *sem,
			const struct timespec *timeout)
{
	int ret;

	if (timeout == NULL)
		return -EINVAL;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	return timedget_sem(sem, timeout);
}

int evl_get_sem(struct evl_sem *sem)
{
	int ret;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	return timedget_sem(sem, NULL);
}

int evl_tryget_sem(struct evl_sem *sem)
{
	struct timespec zerotime = { .tv_sec = 0, .tv_nsec = 0 };
	int ret;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	ret = timedget_sem(sem, &zerotime);
	if (ret == -ETIMEDOUT)
		return -EAGAIN;

	return ret;
}

int evl_put_sem(struct evl_sem *sem)
{
	uint64_t val = (uint64_t)1U;
	int ret;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	ret = write(sem->active.fd, &val, sizeof(val));
	if (ret != sizeof(val))
		return -errno;

	return 0;
}
