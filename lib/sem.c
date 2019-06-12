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
#include <evl/atomic.h>
#include <evl/evl.h>
#include <evl/sem.h>
#include <evl/thread.h>
#include <evl/syscall.h>
#include <linux/types.h>
#include <uapi/evl/factory.h>
#include "internal.h"

#define __SEM_ACTIVE_MAGIC	0xcb13cb13
#define __SEM_DEAD_MAGIC	0

int evl_new_sem(struct evl_sem *sem, int clockfd, int initval,
		const char *fmt, ...)
{
	struct evl_monitor_attrs attrs;
	struct evl_element_ids eids;
	int efd, ret;
	va_list ap;
	char *name;

	if (evl_shared_memory == NULL)
		return -ENXIO;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.type = EVL_MONITOR_EVENT;
	attrs.protocol = EVL_EVENT_COUNT;
	attrs.clockfd = clockfd;
	attrs.initval = initval;
	efd = create_evl_element(EVL_MONITOR_DEV, name, &attrs, &eids);
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
	struct evl_monitor_binding bind;
	int ret, efd;
	va_list ap;

	va_start(ap, fmt);
	efd = open_evl_element_vargs(EVL_MONITOR_DEV, fmt, ap);
	va_end(ap);
	if (efd < 0)
		return efd;

	ret = ioctl(efd, EVL_MONIOC_BIND, &bind);
	if (ret) {
		ret = -errno;
		goto fail;
	}

	if (bind.type != EVL_MONITOR_EVENT ||
		bind.protocol != EVL_EVENT_COUNT) {
		ret = -EINVAL;
		goto fail;
	}

	sem->active.state = evl_shared_memory + bind.eids.state_offset;
	sem->active.fundle = bind.eids.fundle;
	sem->active.efd = efd;
	sem->magic = __SEM_ACTIVE_MAGIC;

	return 0;
fail:
	close(efd);

	return ret;
}

int evl_close_sem(struct evl_sem *sem)
{
	int ret;

	if (sem->magic == __SEM_UNINIT_MAGIC)
		return 0;

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

static int check_sanity(struct evl_sem *sem)
{
	if (sem->magic == __SEM_UNINIT_MAGIC)
		return evl_new_sem(sem,
				   sem->uninit.clockfd,
				   sem->uninit.initval,
				   sem->uninit.name);

	return sem->magic != __SEM_ACTIVE_MAGIC ? -EINVAL : 0;
}

/*
 * CAUTION: we assume that the implementation of atomic_cmpxchg()
 * which currently relies on GCC's __sync_val_compare_and_swap()
 * built-in does issue proper full memory barrier on successful swap,
 * so we should not have to emit them manually.
 */
static int try_get(struct evl_monitor_state *state)
{
	int val, prev, next;

	val = atomic_read(&state->u.event.value);
	if (val <= 0)
		return -EAGAIN;

	do {
		prev = val;
		next = prev - 1;
		val = atomic_cmpxchg(&state->u.event.value, prev, next);
		/*
		 * If the semaphore's value was strictly positive and
		 * we end up with a negative one after a swap attempt,
		 * then cmpxchg must have failed, and the non-blocking
		 * P operation failed.
		 */
		if (val <= 0)
			return -EAGAIN;
	} while (val != prev);

	return 0;
}

int evl_timedget(struct evl_sem *sem, const struct timespec *timeout)
{
	struct evl_monitor_state *state;
	struct evl_monitor_waitreq req;
	int mode, ret, cancel_type;
	fundle_t current;

	current = evl_get_current();
	if (current == EVL_NO_HANDLE)
		return -EPERM;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	/*
	 * Threads running in-band and/or enabling some debug features
	 * must go through the slow syscall path.
	 */
	state = sem->active.state;
	mode = evl_get_current_mode();
	if (!(mode & (T_INBAND|T_WEAK|T_DEBUG))) {
		ret = try_get(state);
		if (ret != -EAGAIN)
			return ret;
	}

	req.gatefd = -1;
	req.timeout = *timeout;
	req.status = -EINVAL;
	req.value = 0;		/* dummy */

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &cancel_type);
	ret = oob_ioctl(sem->active.efd, EVL_MONIOC_WAIT, &req);
	pthread_setcanceltype(cancel_type, NULL);

	return ret ? -errno : req.status;
}

int evl_get(struct evl_sem *sem)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedget(sem, &timeout);
}

int evl_tryget(struct evl_sem *sem)
{
	int ret;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	return try_get(sem->active.state);
}

int evl_put(struct evl_sem *sem)
{
	struct evl_monitor_state *state;
	int val, prev, next, ret;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	state = sem->active.state;
	val = atomic_read(&state->u.event.value);
	if (val < 0) {
	slow_path:
		if (evl_get_current())
			ret = oob_ioctl(sem->active.efd, EVL_MONIOC_SIGNAL, NULL);
		else
			/* In-band threads may post pended sema4s. */
			ret = ioctl(sem->active.efd, EVL_MONIOC_SIGNAL, NULL);
		return ret ? -errno : 0;
	}

	do {
		prev = val;
		next = prev + 1;
		val = atomic_cmpxchg(&state->u.event.value, prev, next);
		/* Check if somebody sneaked in the wait queue. */
		if (val < 0)
			goto slow_path;
	} while (val != prev);

	return 0;
}

int evl_getval(struct evl_sem *sem)
{
	if (sem->magic != __SEM_ACTIVE_MAGIC)
		return -EINVAL;

	return atomic_read(&sem->active.state->u.event.value);
}
