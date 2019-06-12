/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
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
#include <evl/flags.h>
#include <evl/thread.h>
#include <evl/syscall.h>
#include <linux/types.h>
#include <uapi/evl/factory.h>
#include "internal.h"

#define __FLAGS_ACTIVE_MAGIC	0xb42bb42b
#define __FLAGS_DEAD_MAGIC	0

int evl_new_flags(struct evl_flags *flg, int clockfd, int initval,
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
	attrs.protocol = EVL_EVENT_MASK;
	attrs.clockfd = clockfd;
	attrs.initval = initval;
	efd = create_evl_element(EVL_MONITOR_DEV, name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	flg->active.state = evl_shared_memory + eids.state_offset;
	flg->active.fundle = eids.fundle;
	flg->active.efd = efd;
	flg->magic = __FLAGS_ACTIVE_MAGIC;

	return 0;
}

int evl_open_flags(struct evl_flags *flg, const char *fmt, ...)
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
		bind.protocol != EVL_EVENT_MASK) {
		ret = -EINVAL;
		goto fail;
	}

	flg->active.state = evl_shared_memory + bind.eids.state_offset;
	flg->active.fundle = bind.eids.fundle;
	flg->active.efd = efd;
	flg->magic = __FLAGS_ACTIVE_MAGIC;

	return 0;
fail:
	close(efd);

	return ret;
}

int evl_close_flags(struct evl_flags *flg)
{
	int ret;

	if (flg->magic == __FLAGS_UNINIT_MAGIC)
		return 0;

	if (flg->magic != __FLAGS_ACTIVE_MAGIC)
		return -EINVAL;

	ret = close(flg->active.efd);
	if (ret)
		return -errno;

	flg->active.fundle = EVL_NO_HANDLE;
	flg->active.state = NULL;
	flg->magic = __FLAGS_DEAD_MAGIC;

	return 0;
}

static int check_sanity(struct evl_flags *flg)
{
	if (flg->magic == __FLAGS_UNINIT_MAGIC)
		return evl_new_flags(flg,
				   flg->uninit.clockfd,
				   flg->uninit.initval,
				   flg->uninit.name);

	return flg->magic != __FLAGS_ACTIVE_MAGIC ? -EINVAL : 0;
}

static int try_wait(struct evl_monitor_state *state)
{
	int val, prev;

	val = atomic_read(&state->u.event.value);
	if (!val)
		return 0;

	do {
		prev = val;
		val = atomic_cmpxchg(&state->u.event.value, prev, 0);
		if (!val)
			return 0;
	} while (val != prev);

	return val;
}

int evl_timedwait_flags(struct evl_flags *flg,
			const struct timespec *timeout,	int *r_bits)
{
	struct evl_monitor_state *state;
	struct evl_monitor_waitreq req;
	int mode, ret, cancel_type;
	fundle_t current;

	current = evl_get_current();
	if (current == EVL_NO_HANDLE)
		return -EPERM;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	/*
	 * Threads running in-band and/or enabling some debug features
	 * must go through the slow syscall path.
	 */
	state = flg->active.state;
	mode = evl_get_current_mode();
	if (!(mode & (T_INBAND|T_WEAK|T_DEBUG))) {
		ret = try_wait(state);
		if (ret) {
			*r_bits = ret;
			return 0;
		}
	}

	req.gatefd = -1;
	req.timeout = *timeout;
	req.status = -EINVAL;
	req.value = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &cancel_type);
	ret = oob_ioctl(flg->active.efd, EVL_MONIOC_WAIT, &req);
	pthread_setcanceltype(cancel_type, NULL);

	if (ret)
		return -errno;

	if (req.status)
		return req.status;

	*r_bits = req.value;

	return 0;
}

int evl_wait_flags(struct evl_flags *flg, int *r_bits)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedwait_flags(flg, &timeout, r_bits);
}

int evl_trywait_flags(struct evl_flags *flg, int *r_bits)
{
	int ret;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	ret = try_wait(flg->active.state);
	if (!ret)
		return -EAGAIN;

	*r_bits = ret;

	return 0;
}

int evl_post_flags(struct evl_flags *flg, int bits)
{
	struct evl_monitor_state *state;
	int val, prev, next, ret;
	__s32 mask = bits;

	ret = check_sanity(flg);
	if (ret)
		return ret;

	if (!bits)
		return -EINVAL;

	state = flg->active.state;
	val = atomic_read(&state->u.event.value);
	if (!val) {
	slow_path:
		if (evl_get_current())
			ret = oob_ioctl(flg->active.efd, EVL_MONIOC_SIGNAL, &mask);
		else
			ret = ioctl(flg->active.efd, EVL_MONIOC_SIGNAL, &mask);
		return ret ? -errno : 0;
	}

	do {
		prev = val;
		next = prev | bits;
		val = atomic_cmpxchg(&state->u.event.value, prev, next);
		/* Check if somebody sneaked in the wait queue. */
		if (!val)
			goto slow_path;
	} while (val != prev);

	return 0;
}

int evl_peek_flags(struct evl_flags *flg, int *r_bits)
{
	if (flg->magic != __FLAGS_ACTIVE_MAGIC)
		return -EINVAL;

	*r_bits = atomic_read(&flg->active.state->u.event.value);

	return 0;
}
