/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdbool.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <evl/compiler.h>
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

int evl_new_sem_any(struct evl_sem *sem, int clockfd, int initval,
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

	sem->u.active.state = evl_shared_memory + eids.state_offset;
	/* Force sync the PTE. */
	atomic_set(&sem->u.active.state->u.event.value, initval);
	sem->u.active.fundle = eids.fundle;
	sem->u.active.efd = efd;
	sem->magic = __SEM_ACTIVE_MAGIC;

	return efd;
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

	sem->u.active.state = evl_shared_memory + bind.eids.state_offset;
	__force_read_access(sem->u.active.state->u.event.value);
	sem->u.active.fundle = bind.eids.fundle;
	sem->u.active.efd = efd;
	sem->magic = __SEM_ACTIVE_MAGIC;

	return efd;
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

	ret = close(sem->u.active.efd);
	if (ret)
		return -errno;

	sem->u.active.fundle = EVL_NO_HANDLE;
	sem->u.active.state = NULL;
	sem->magic = __SEM_DEAD_MAGIC;

	return 0;
}

static int check_sanity(struct evl_sem *sem)
{
	int efd;

	if (sem->magic == __SEM_UNINIT_MAGIC) {
		efd = evl_new_sem_any(sem,
				sem->u.uninit.clockfd,
				sem->u.uninit.initval,
				sem->u.uninit.name);
		return efd < 0 ? efd : 0;
	}

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

int evl_timedget_sem(struct evl_sem *sem, const struct timespec *timeout)
{
	struct evl_monitor_state *state;
	struct evl_monitor_waitreq req;
	struct __evl_timespec kts;
	fundle_t current;
	int ret;

	current = evl_get_current();
	if (current == EVL_NO_HANDLE)
		return -EPERM;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	state = sem->u.active.state;
	ret = try_get(state);
	if (ret != -EAGAIN)
		return ret;

	req.gatefd = -1;
	req.timeout_ptr = __evl_ktimespec_ptr64(timeout, kts);
	req.status = -EINVAL;
	req.value = 0;		/* dummy */

	ret = oob_ioctl(sem->u.active.efd, EVL_MONIOC_WAIT, &req);

	return ret ? -errno : req.status;
}

int evl_get_sem(struct evl_sem *sem)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedget_sem(sem, &timeout);
}

int evl_tryget_sem(struct evl_sem *sem)
{
	int ret;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	return try_get(sem->u.active.state);
}

static inline bool is_polled(struct evl_monitor_state *state)
{
	return !!atomic_read(&state->u.event.pollrefs);
}

int evl_put_sem(struct evl_sem *sem)
{
	struct evl_monitor_state *state;
	int val, prev, next, ret;
	__s32 sigval = 1;

	ret = check_sanity(sem);
	if (ret)
		return ret;

	state = sem->u.active.state;
	val = atomic_read(&state->u.event.value);
	if (val < 0 || is_polled(state)) {
	slow_path:
		if (evl_get_current())
			ret = oob_ioctl(sem->u.active.efd,
					EVL_MONIOC_SIGNAL, &sigval);
		else
			/* In-band threads may post pended sema4s. */
			ret = ioctl(sem->u.active.efd,
				EVL_MONIOC_SIGNAL, &sigval);
		return ret ? -errno : 0;
	}

	do {
		prev = val;
		next = prev + 1;
		val = atomic_cmpxchg(&state->u.event.value, prev, next);
		/*
		 * If somebody sneaked in the wait queue or started
		 * polling us in the meantime, we have to perform a
		 * kernel entry.
		 */
		if (val < 0)
			goto slow_path;
		if (is_polled(state)) {
			/* If swap happened, just trigger a wakeup. */
			if (val == prev)
				sigval = 0;
			goto slow_path;
		}
	} while (val != prev);

	return 0;
}

int evl_peek_sem(struct evl_sem *sem, int *r_val)
{
	if (sem->magic != __SEM_ACTIVE_MAGIC)
		return -EINVAL;

	*r_val = atomic_read(&sem->u.active.state->u.event.value);

	return 0;
}
