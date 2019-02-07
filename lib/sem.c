/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <evenless/atomic.h>
#include <evenless/evl.h>
#include <evenless/sem.h>
#include <evenless/thread.h>
#include <evenless/syscall.h>
#include <linux/types.h>
#include <uapi/evenless/factory.h>
#include "internal.h"

int evl_new_sem(struct evl_sem *sem,
		int flags, int clockfd, int initval,
		const char *fmt, ...)
{
	struct evl_element_ids eids;
	struct evl_sem_attrs attrs;
	int efd, ret;
	va_list ap;
	char *name;

	if (evl_shared_memory == NULL)
		return -EAGAIN;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.clockfd = clockfd;
	attrs.flags = flags;
	attrs.initval = initval;
	efd = create_evl_element("semaphore", name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	sem->active.state = evl_shared_memory + eids.state_offset;
	sem->active.fundle = eids.fundle;
	sem->active.efd = efd;
	sem->magic = __SEM_ACTIVE_MAGIC;

	return 0;
}

int evl_open_sem(struct evl_sem *sem, const char *fmt, ...)
{
	struct evl_element_ids eids;
	int ret, efd;
	va_list ap;

	va_start(ap, fmt);
	efd = open_evl_element("semaphore", fmt, ap);
	va_end(ap);
	if (efd < 0)
		return efd;

	ret = ioctl(efd, EVL_SEMIOC_BIND, &eids);
	if (ret)
		return -errno;

	sem->active.state = evl_shared_memory + eids.state_offset;
	sem->active.fundle = eids.fundle;
	sem->active.efd = efd;
	sem->magic = __SEM_ACTIVE_MAGIC;

	return 0;
}

int evl_release_sem(struct evl_sem *sem)
{
	int ret;

	if (sem->magic != __SEM_ACTIVE_MAGIC)
		return -EINVAL;

	ret = close(sem->active.efd);
	if (ret)
		return -errno;

	sem->active.fundle = EVL_NO_HANDLE;
	sem->active.state = NULL;
	sem->magic = __SEM_DEAD_MAGIC;

	return 0;
}

static int try_get(struct evl_sem_state *state, int count)
{
	int curval, oldval, newval;

	curval = atomic_read(&state->value);
	if (curval <= 0)
		return -EAGAIN;

	do {
		oldval = curval;
		newval = oldval - count;
		if (newval > oldval)
			return -EINVAL;
		curval = atomic_cmpxchg(&state->value, oldval, newval);
		/* Did somebody else deplete the sema4? */
		if (curval <= 0)
			return -EAGAIN;
	} while (curval != oldval);

	smp_mb();

	return 0;
}

static int check_sanity(struct evl_sem *sem, int count)
{
	if (count <= 0)
		return -EINVAL;

	if (sem->magic == __SEM_UNINIT_MAGIC)
		return evl_new_sem(sem,
				   sem->uninit.flags,
				   sem->uninit.clockfd,
				   sem->uninit.initval,
				   sem->uninit.name);

	return sem->magic != __SEM_ACTIVE_MAGIC ? -EINVAL : 0;
}

int evl_get_sem_timed(struct evl_sem *sem, int count,
		const struct timespec *timeout)
{
	struct evl_sem_waitreq req;
	int mode, ret, cancel_type;
	fundle_t current;

	current = evl_get_current();
	if (current == EVL_NO_HANDLE)
		return -EPERM;

	ret = check_sanity(sem, count);
	if (ret)
		return ret;

	/*
	 * Threads running in-band and/or enabling some debug features
	 * must go through the slow syscall path.
	 */
	mode = evl_get_current_mode();
	if (!(mode & (T_INBAND|T_WEAK|T_DEBUG))) {
		ret = try_get(sem->active.state, count);
		if (ret != -EAGAIN)
			return ret;
	}

	req.timeout = *timeout;
	req.count = count;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &cancel_type);
	ret = oob_ioctl(sem->active.efd, EVL_SEMIOC_GET, &req);
	pthread_setcanceltype(cancel_type, NULL);

	return ret ? -errno : 0;
}

int evl_get_sem(struct evl_sem *sem, int count)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_get_sem_timed(sem, count, &timeout);
}

int evl_tryget_sem(struct evl_sem *sem, int count)
{
	int ret;

	ret = check_sanity(sem, count);
	if (ret)
		return ret;

	return try_get(sem->active.state, count);
}

int evl_put_sem(struct evl_sem *sem, int count)
{
	int curval, oldval, newval, ret;
	struct evl_sem_state *state;
	__s32 val;

	ret = check_sanity(sem, count);
	if (ret)
		return ret;

	state = sem->active.state;
	curval = atomic_read(&state->value);
	if (curval < 0) {
	slow_path:
		val = count;
		if (evl_get_current())
			ret = oob_ioctl(sem->active.efd, EVL_SEMIOC_PUT, &val);
		else
			/* In-band threads may post pended sema4s. */
			ret = ioctl(sem->active.efd, EVL_SEMIOC_PUT, &val);
		return ret ? -errno : 0;
	}

	if (state->flags & EVL_SEM_PULSE)
		return 0;

	do {
		oldval = curval;
		newval = oldval + count;
		if (newval < oldval)
			return -EINVAL;
		curval = atomic_cmpxchg(&state->value, oldval, newval);
		/* Check if somebody sneaked in the wait queue. */
		if (curval < 0)
			goto slow_path;
	} while (curval != oldval);

	smp_mb();

	return 0;
}

int evl_get_semval(struct evl_sem *sem)
{
	if (sem->magic != __SEM_ACTIVE_MAGIC)
		return -EINVAL;

	return atomic_read(&sem->active.state->value);
}
