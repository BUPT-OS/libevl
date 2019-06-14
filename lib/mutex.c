/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <evl/atomic.h>
#include <evl/evl.h>
#include <evl/mutex.h>
#include <evl/thread.h>
#include <evl/syscall.h>
#include <linux/types.h>
#include <uapi/evl/factory.h>
#include <uapi/evl/mutex.h>
#include "internal.h"

#define __MUTEX_ACTIVE_MAGIC	0xab12ab12
#define __MUTEX_DEAD_MAGIC	0

static int init_mutex_vargs(struct evl_mutex *mutex,
			int protocol, int clockfd,
			unsigned int ceiling,
			const char *fmt, va_list ap)
{
	struct evl_monitor_attrs attrs;
	struct evl_element_ids eids;
	int efd, ret;
	char *name;

	if (evl_shared_memory == NULL)
		return -ENXIO;

	/*
	 * We align on the in-band SCHED_FIFO priority range. Although
	 * the core does not require this, a 1:1 mapping between
	 * in-band and out-of-band priorities is simpler to deal with
	 * for users with respect to inband <-> out-of-band mode
	 * switches.
	 */
	if (protocol == EVL_GATE_PP) {
		ret = sched_get_priority_max(SCHED_FIFO);
		if (ret < 0 || ceiling > ret)
			return -EINVAL;
	}

	ret = vasprintf(&name, fmt, ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.type = EVL_MONITOR_GATE;
	attrs.protocol = protocol;
	attrs.clockfd = clockfd;
	attrs.initval = ceiling;
	efd = create_evl_element(EVL_MONITOR_DEV, name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	mutex->active.state = evl_shared_memory + eids.state_offset;
	mutex->active.fundle = eids.fundle;
	mutex->active.type = EVL_MONITOR_GATE;
	mutex->active.protocol = protocol;
	mutex->active.efd = efd;
	mutex->magic = __MUTEX_ACTIVE_MAGIC;

	return 0;
}

static int init_mutex(struct evl_mutex *mutex,
		int protocol, int clockfd, unsigned int ceiling,
		const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_mutex_vargs(mutex, protocol, clockfd, ceiling, fmt, ap);
	va_end(ap);

	return ret;
}

static int open_mutex_vargs(struct evl_mutex *mutex,
			const char *fmt, va_list ap)
{
	struct evl_monitor_binding bind;
	int ret, efd;

	efd = open_evl_element_vargs(EVL_MONITOR_DEV, fmt, ap);
	if (efd < 0)
		return efd;

	ret = ioctl(efd, EVL_MONIOC_BIND, &bind);
	if (ret) {
		ret = -errno;
		goto fail;
	}

	if (bind.type != EVL_MONITOR_GATE) {
		ret = -EINVAL;
		goto fail;
	}

	mutex->active.state = evl_shared_memory + bind.eids.state_offset;
	mutex->active.fundle = bind.eids.fundle;
	mutex->active.type = bind.type;
	mutex->active.protocol = bind.protocol;
	mutex->active.efd = efd;
	mutex->magic = __MUTEX_ACTIVE_MAGIC;

	return 0;
fail:
	close(efd);

	return ret;
}

int evl_new_mutex(struct evl_mutex *mutex, int clockfd,
		const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_mutex_vargs(mutex, EVL_GATE_PI, clockfd, 0, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_new_mutex_ceiling(struct evl_mutex *mutex, int clockfd,
			unsigned int ceiling,
			const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_mutex_vargs(mutex, EVL_GATE_PP, clockfd, ceiling, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_open_mutex(struct evl_mutex *mutex, const char *fmt, ...)
{
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = open_mutex_vargs(mutex, fmt, ap);
	va_end(ap);

	return efd;
}

int evl_close_mutex(struct evl_mutex *mutex)
{
	int efd;

	if (mutex->magic == __MUTEX_UNINIT_MAGIC)
		return 0;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	efd = mutex->active.efd;
	mutex->active.efd = -1;
	compiler_barrier();
	close(efd);

	mutex->active.fundle = EVL_NO_HANDLE;
	mutex->active.state = NULL;
	mutex->magic = __MUTEX_DEAD_MAGIC;

	return 0;
}

static int try_lock(struct evl_mutex *mutex)
{
	struct evl_user_window *u_window;
	struct evl_monitor_state *gst;
	bool protect = false;
	fundle_t current;
	int mode, ret;

	current = evl_get_current();
	if (current == EVL_NO_HANDLE)
		return -EPERM;

	if (mutex->magic == __MUTEX_UNINIT_MAGIC &&
		mutex->uninit.type == EVL_MONITOR_GATE) {
		ret = init_mutex(mutex,
				mutex->uninit.protocol,
				mutex->uninit.clockfd,
				mutex->uninit.ceiling,
				mutex->uninit.name);
		if (ret)
			return ret;
	} else if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	gst = mutex->active.state;

	/*
	 * Threads running in-band and/or enabling some debug features
	 * must go through the slow syscall path.
	 */
	mode = evl_get_current_mode();
	if (!(mode & (T_INBAND|T_WEAK|T_DEBUG))) {
		if (mutex->active.protocol == EVL_GATE_PP) {
			u_window = evl_get_current_window();
			/*
			 * Can't nest lazy ceiling requests, have to
			 * take the slow path when this happens.
			 */
			if (u_window->pp_pending != EVL_NO_HANDLE)
				goto slow_path;
			u_window->pp_pending = mutex->active.fundle;
			protect = true;
		}
		ret = evl_fast_lock_mutex(&gst->u.gate.owner, current);
		if (ret == 0) {
			gst->flags &= ~EVL_MONITOR_SIGNALED;
			return 0;
		}
	} else {
	slow_path:
		ret = 0;
		if (evl_is_mutex_owner(&gst->u.gate.owner, current))
			ret = -EBUSY;
	}

	if (ret == -EBUSY) {
		if (protect)
			u_window->pp_pending = EVL_NO_HANDLE;

		return -EDEADLK;
	}

	return -ENODATA;
}

int evl_timedlock_mutex(struct evl_mutex *mutex,
			const struct timespec *timeout)
{
	struct evl_monitor_lockreq lreq;
	int ret;

	ret = try_lock(mutex);
	if (ret != -ENODATA)
		return ret;

	lreq.timeout = *timeout;

	do
		ret = oob_ioctl(mutex->active.efd, EVL_MONIOC_ENTER, &lreq);
	while (ret && errno == EINTR);

	return ret ? -errno : 0;
}

int evl_lock_mutex(struct evl_mutex *mutex)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedlock_mutex(mutex, &timeout);
}

int evl_trylock_mutex(struct evl_mutex *mutex)
{
	int ret;

	ret = try_lock(mutex);
	if (ret != -ENODATA)
		return ret;

	do
		ret = oob_ioctl(mutex->active.efd, EVL_MONIOC_TRYENTER);
	while (ret && errno == EINTR);

	return ret ? -errno : 0;
}

int evl_unlock_mutex(struct evl_mutex *mutex)
{
	struct evl_user_window *u_window;
	struct evl_monitor_state *gst;
	fundle_t current;
	int ret, mode;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	gst = mutex->active.state;
	current = evl_get_current();
	if (!evl_is_mutex_owner(&gst->u.gate.owner, current))
		return -EPERM;

	/* Do we have waiters on a signaled event we are gating? */
	if (gst->flags & EVL_MONITOR_SIGNALED)
		goto slow_path;

	mode = evl_get_current_mode();
	if (mode & (T_WEAK|T_DEBUG))
		goto slow_path;

	if (evl_fast_unlock_mutex(&gst->u.gate.owner, current)) {
		if (mutex->active.protocol == EVL_GATE_PP) {
			u_window = evl_get_current_window();
			u_window->pp_pending = EVL_NO_HANDLE;
		}
		return 0;
	}

	/*
	 * If the fast release failed, somebody else must be waiting
	 * for entering the lock or PP was committed for the current
	 * thread. Need to ask the kernel for proper release.
	 */
slow_path:
	ret = oob_ioctl(mutex->active.efd, EVL_MONIOC_EXIT);

	return ret ? -errno : 0;
}

int evl_set_mutex_ceiling(struct evl_mutex *mutex,
			unsigned int ceiling)
{
	int ret;

	if (ceiling == 0)
		return -EINVAL;

	ret = sched_get_priority_max(SCHED_FIFO);
	if (ret < 0 || ceiling > ret)
		return -EINVAL;

	if (mutex->magic == __MUTEX_UNINIT_MAGIC) {
		if (mutex->uninit.type != EVL_MONITOR_GATE ||
			mutex->uninit.protocol != EVL_GATE_PP)
			return -EINVAL;
		mutex->uninit.ceiling = ceiling;
		return 0;
	}

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC ||
		mutex->active.type != EVL_MONITOR_GATE ||
		mutex->active.protocol != EVL_GATE_PP) {
		return -EINVAL;
	}

	mutex->active.state->u.gate.ceiling = ceiling;

	return 0;
}

int evl_get_mutex_ceiling(struct evl_mutex *mutex)
{
	if (mutex->magic == __MUTEX_UNINIT_MAGIC) {
		if (mutex->uninit.type != EVL_MONITOR_GATE ||
			mutex->uninit.protocol != EVL_GATE_PP)
			return -EINVAL;

		return mutex->uninit.ceiling;
	}

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC ||
		mutex->active.type != EVL_MONITOR_GATE ||
		mutex->active.protocol != EVL_GATE_PP)
		return -EINVAL;

	return mutex->active.state->u.gate.ceiling;
}
