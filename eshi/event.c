/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <unistd.h>
#include <evl/event.h>
#include <sys/eventfd.h>
#include "internal.h"

#define __EVENT_ACTIVE_MAGIC	0xef55ef55
#define __EVENT_DEAD_MAGIC	0

int evl_create_event(struct evl_event *evt, int clockfd, int flags,
		const char *fmt, ...)
{
	pthread_condattr_t attr;
	int ret, fd;

	if (!eshi_is_initialized())
		return -ENXIO;

	if (clockfd != EVL_CLOCK_MONOTONIC &&
		clockfd != EVL_CLOCK_REALTIME)
		return -EINVAL;

	fd = eventfd(0, EFD_CLOEXEC);
	if (fd < 0)
		return -errno;

	pthread_condattr_init(&attr);
	pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);
	pthread_condattr_setclock(&attr, -clockfd);
	ret = pthread_cond_init(&evt->active.cond, &attr);
	pthread_condattr_destroy(&attr);
	if (ret) {
		close(fd);
		return -ret;
	}

	evt->active.fd = fd;
	evt->magic = __EVENT_ACTIVE_MAGIC;

	return fd;
}

static int check_sanity(struct evl_event *evt)
{
	int ret = 0, fd;

	if (evt->magic == __EVENT_UNINIT_MAGIC) {
		fd = evl_create_event(evt,
				evt->uninit.clockfd,
				evt->uninit.flags,
				evt->uninit.name);
		return fd < 0 ? fd : 0;
	} else if (evt->magic != __EVENT_ACTIVE_MAGIC)
		return -EINVAL;

	return ret;
}

int evl_wait_event(struct evl_event *evt, struct evl_mutex *mutex)
{
	int ret;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	ret = check_sanity(evt);
	if (ret)
		return ret;

	return -pthread_cond_wait(&evt->active.cond,
				&mutex->active.mutex);
}

int evl_timedwait_event(struct evl_event *evt,
			struct evl_mutex *mutex,
			const struct timespec *timeout)
{
	int ret;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	ret = check_sanity(evt);
	if (ret)
		return ret;

	return -pthread_cond_timedwait(&evt->active.cond,
				&mutex->active.mutex, timeout);
}

int evl_signal_event(struct evl_event *evt)
{
	int ret;

	ret = check_sanity(evt);
	if (ret)
		return ret;

	return -pthread_cond_signal(&evt->active.cond);
}

int evl_broadcast_event(struct evl_event *evt)
{
	int ret;

	ret = check_sanity(evt);
	if (ret)
		return ret;

	return -pthread_cond_broadcast(&evt->active.cond);
}

int evl_close_event(struct evl_event *evt)
{
	if (evt->magic == __EVENT_UNINIT_MAGIC)
		return 0;

	if (evt->magic != __EVENT_ACTIVE_MAGIC)
		return -EINVAL;

	close(evt->active.fd);
	evt->magic = __EVENT_DEAD_MAGIC;

	return -pthread_cond_destroy(&evt->active.cond);
}
