/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <unistd.h>
#include <evl/mutex.h>
#include <sys/eventfd.h>
#include "internal.h"

#define __MUTEX_ACTIVE_MAGIC	0xab12ab12
#define __MUTEX_DEAD_MAGIC	0

static int create_mutex(struct evl_mutex *mutex, int type,
			int clockfd, int ceiling)
{
	int ret, fd, protocol, ptype;
	pthread_mutexattr_t attr;

	if (!eshi_is_initialized())
		return -ENXIO;

	switch (clockfd) {
	case EVL_CLOCK_MONOTONIC:
		mutex->active.clock = CLOCK_MONOTONIC;
		break;
	case EVL_CLOCK_REALTIME:
		mutex->active.clock = CLOCK_REALTIME;
		break;
	default:
		return -EINVAL;
	}

	switch (type) {
	case EVL_MUTEX_NORMAL:
		ptype = PTHREAD_MUTEX_NORMAL;
		break;
	case EVL_MUTEX_RECURSIVE:
		ptype = PTHREAD_MUTEX_RECURSIVE;
		break;
	default:
		return -EINVAL;
	}

	fd = eventfd(1, EFD_CLOEXEC); /* Set to always readable. */
	if (fd < 0)
		return -errno;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, ptype);
	protocol = PTHREAD_PRIO_INHERIT;
	if (ceiling) {
		protocol = PTHREAD_PRIO_PROTECT;
		pthread_mutexattr_setprioceiling(&attr, ceiling);
	}
	ret = pthread_mutexattr_setprotocol(&attr, protocol);
	if (ret) {
		pthread_mutexattr_destroy(&attr);
		close(fd);
		return -ret;
	}

	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);
	ret = pthread_mutex_init(&mutex->active.mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	if (ret) {
		close(fd);
		return -ret;
	}

	mutex->active.fd = fd;
	mutex->magic = __MUTEX_ACTIVE_MAGIC;

	return 0;
}

int evl_new_mutex_any(struct evl_mutex *mutex, int type,
		int clockfd, unsigned int ceiling,
		const char *fmt, ...)
{
	return create_mutex(mutex, type, clockfd, ceiling) ?:
		mutex->active.fd;
}

static int check_sanity(struct evl_mutex *mutex)
{
	int ret = 0;

	if (mutex->magic == __MUTEX_UNINIT_MAGIC)
		ret = create_mutex(mutex,
				mutex->uninit.type,
				mutex->uninit.clockfd,
				mutex->uninit.ceiling);
	else if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	return ret;
}

int evl_lock_mutex(struct evl_mutex *mutex)
{
	int ret;

	ret = check_sanity(mutex);
	if (ret)
		return ret;

	return -pthread_mutex_lock(&mutex->active.mutex);
}

int evl_timedlock_mutex(struct evl_mutex *mutex,
			const struct timespec *timeout)
{
	const struct timespec *tp = timeout;
	struct timespec ts;
	int ret;

	ret = check_sanity(mutex);
	if (ret)
		return ret;

	if (timeout->tv_sec < 0 || timeout->tv_nsec >= 1000000000L)
		return -EINVAL;

	if (mutex->active.clock == CLOCK_MONOTONIC) {
		timespec_mono_to_real(&ts, timeout);
		tp = &ts;
	}

	return -pthread_mutex_timedlock(&mutex->active.mutex, tp);
}

int evl_trylock_mutex(struct evl_mutex *mutex)
{
	int ret;

	ret = check_sanity(mutex);
	if (ret)
		return ret;

	return -pthread_mutex_trylock(&mutex->active.mutex);
}

int evl_unlock_mutex(struct evl_mutex *mutex)
{
	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	return -pthread_mutex_unlock(&mutex->active.mutex);
}

int evl_set_mutex_ceiling(struct evl_mutex *mutex,
			unsigned int ceiling)
{
	int old;

	if (ceiling == 0)
		return -EINVAL;

	if (mutex->magic == __MUTEX_UNINIT_MAGIC) {
		if (mutex->uninit.ceiling == 0)
			return -EINVAL;

		mutex->uninit.ceiling = ceiling;
	}

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	return -pthread_mutex_setprioceiling(&mutex->active.mutex,
					ceiling, &old);
}

int evl_get_mutex_ceiling(struct evl_mutex *mutex)
{
	int ret, ceiling;

	if (mutex->magic == __MUTEX_UNINIT_MAGIC)
		return mutex->uninit.ceiling;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	ret = pthread_mutex_getprioceiling(&mutex->active.mutex, &ceiling);
	if (ret)
		return ret == EINVAL ? 0 : -ret;

	return ceiling;
}

int evl_close_mutex(struct evl_mutex *mutex)
{
	if (mutex->magic == __MUTEX_UNINIT_MAGIC)
		return 0;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	close(mutex->active.fd);
	mutex->magic = __MUTEX_DEAD_MAGIC;

	return -pthread_mutex_destroy(&mutex->active.mutex);
}
