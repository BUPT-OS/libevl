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
#include <evenless/evl.h>
#include <evenless/mutex.h>
#include <evenless/condvar.h>
#include <evenless/thread.h>
#include <evenless/syscall.h>
#include <linux/types.h>
#include <uapi/evenless/factory.h>
#include <uapi/evenless/mutex.h>
#include "internal.h"

#define __CONDVAR_ACTIVE_MAGIC	0xef55ef55
#define __CONDVAR_DEAD_MAGIC	0

static int init_condvar_vargs(struct evl_condvar *cv,
			int clockfd, const char *fmt, va_list ap)
{
	struct evl_monitor_attrs attrs;
	struct evl_element_ids eids;
	int efd, ret;
	char *name;

	if (evl_shared_memory == NULL)
		return -ENXIO;

	ret = vasprintf(&name, fmt, ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.type = EVL_MONITOR_EV;
	attrs.clockfd = clockfd;
	attrs.initval = 0;
	efd = create_evl_element("monitor", name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	cv->active.state = evl_shared_memory + eids.state_offset;
	cv->active.fundle = eids.fundle;
	cv->active.efd = efd;
	cv->magic = __CONDVAR_ACTIVE_MAGIC;

	return 0;
}

static int init_condvar(struct evl_condvar *cv,
			int clockfd, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_condvar_vargs(cv, clockfd, fmt, ap);
	va_end(ap);

	return ret;
}

static int open_condvar_vargs(struct evl_condvar *cv,
			const char *fmt, va_list ap)
{
	struct evl_monitor_binding bind;
	int ret, efd;

	efd = open_evl_element_vargs("monitor", fmt, ap);
	if (efd < 0)
		return efd;

	ret = ioctl(efd, EVL_MONIOC_BIND, &bind);
	if (ret) {
		ret = -errno;
		goto fail;
	}

	if (bind.type != EVL_MONITOR_EV) {
		ret = -EINVAL;
		goto fail;
	}

	cv->active.state = evl_shared_memory + bind.eids.state_offset;
	cv->active.fundle = bind.eids.fundle;
	cv->active.efd = efd;
	cv->magic = __CONDVAR_ACTIVE_MAGIC;

	return 0;
fail:
	close(efd);

	return ret;
}

int evl_new_condvar(struct evl_condvar *cv,
		int clockfd, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_condvar_vargs(cv, clockfd, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_open_condvar(struct evl_condvar *cv, const char *fmt, ...)
{
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = open_condvar_vargs(cv, fmt, ap);
	va_end(ap);

	return efd;
}

int evl_close_condvar(struct evl_condvar *cv)
{
	int efd;

	if (cv->magic != __CONDVAR_ACTIVE_MAGIC)
		return -EINVAL;

	efd = cv->active.efd;
	cv->active.efd = -1;
	compiler_barrier();
	close(efd);

	cv->active.fundle = EVL_NO_HANDLE;
	cv->active.state = NULL;
	cv->magic = __CONDVAR_DEAD_MAGIC;

	return 0;
}

static int check_condvar_sanity(struct evl_condvar *cv)
{
	int ret;

	if (cv->magic == __CONDVAR_UNINIT_MAGIC) {
		ret = init_condvar(cv, cv->uninit.clockfd, cv->uninit.name);
		if (ret)
			return ret;
	} else if (cv->magic != __CONDVAR_ACTIVE_MAGIC)
		return -EINVAL;

	return 0;
}

static struct evl_monitor_state *get_lock_state(struct evl_condvar *cv)
{
	struct evl_monitor_state *est = cv->active.state;

	if (est->u.event.gate_offset == EVL_MONITOR_NOGATE)
		return NULL;	/* Nobody waits on @cv */

	return evl_shared_memory + est->u.event.gate_offset;
}

struct unwait_data {
	struct evl_monitor_unwaitreq ureq;
	int efd;
};

static void unwait_condvar(void *data)
{
	struct unwait_data *unwait = data;
	int ret;

	do
		ret = oob_ioctl(unwait->efd, EVL_MONIOC_UNWAIT,
				&unwait->ureq);
	while (ret && errno == EINTR);
}

int evl_timedwait(struct evl_condvar *cv,
		struct evl_mutex *mutex,
		const struct timespec *timeout)
{
	struct evl_monitor_waitreq req;
	struct unwait_data unwait;
	int ret, old_type;

	ret = check_condvar_sanity(cv);
	if (ret)
		return ret;

	req.gatefd = mutex->active.efd;
	req.timeout = *timeout;
	req.status = -EINVAL;
	unwait.ureq.gatefd = req.gatefd;
	unwait.efd = cv->active.efd;

	pthread_cleanup_push(unwait_condvar, &unwait);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);
	ret = oob_ioctl(cv->active.efd, EVL_MONIOC_WAIT, &req);
	pthread_setcanceltype(old_type, NULL);
	pthread_cleanup_pop(0);

	/*
	 * If oob_ioctl() failed with EINTR, we got forcibly unblocked
	 * while waiting for the condvar or trying to reacquire the lock
	 * afterwards, in which case the mutex was left
	 * unlocked. Issue UNWAIT to recover from that situation.
	 */
	if (ret && errno == EINTR) {
		unwait_condvar(&unwait);
		pthread_testcancel();
	}

	return ret ? -errno : req.status;
}

int evl_wait(struct evl_condvar *cv, struct evl_mutex *mutex)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedwait(cv, mutex, &timeout);
}

int evl_signal(struct evl_condvar *cv)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_condvar_sanity(cv);
	if (ret)
		return ret;

	gst = get_lock_state(cv);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = cv->active.state;
		est->flags |= EVL_MONITOR_SIGNALED;
	}

	return 0;
}

int evl_signal_thread(struct evl_condvar *cv, int thrfd)
{
	struct evl_monitor_state *gst;
	__u32 efd;
	int ret;

	ret = check_condvar_sanity(cv);
	if (ret)
		return ret;

	gst = get_lock_state(cv);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		efd = cv->active.efd;
	}

	return oob_ioctl(thrfd, EVL_THRIOC_SIGNAL, &efd) ? -errno : 0;
}

int evl_broadcast(struct evl_condvar *cv)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_condvar_sanity(cv);
	if (ret)
		return ret;

	gst = get_lock_state(cv);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = cv->active.state;
		est->flags |= EVL_MONITOR_SIGNALED|EVL_MONITOR_BROADCAST;
	}

	return 0;
}
