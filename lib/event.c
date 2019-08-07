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
#include <evl/evl.h>
#include <evl/mutex.h>
#include <evl/event.h>
#include <evl/thread.h>
#include <evl/syscall.h>
#include <linux/types.h>
#include <uapi/evl/factory.h>
#include <uapi/evl/mutex.h>
#include "internal.h"

#define __EVENT_ACTIVE_MAGIC	0xef55ef55
#define __EVENT_DEAD_MAGIC	0

static int init_event_vargs(struct evl_event *evt,
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

	attrs.type = EVL_MONITOR_EVENT;
	attrs.protocol = EVL_EVENT_GATED;
	attrs.clockfd = clockfd;
	attrs.initval = 0;
	efd = create_evl_element(EVL_MONITOR_DEV, name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	evt->active.state = evl_shared_memory + eids.state_offset;
	evt->active.fundle = eids.fundle;
	evt->active.efd = efd;
	evt->magic = __EVENT_ACTIVE_MAGIC;

	return efd;
}

static int init_event(struct evl_event *evt,
		int clockfd, const char *fmt, ...)
{
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = init_event_vargs(evt, clockfd, fmt, ap);
	va_end(ap);

	return efd;
}

static int open_event_vargs(struct evl_event *evt,
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

	if (bind.type != EVL_MONITOR_EVENT ||
		bind.protocol != EVL_EVENT_GATED) {
		ret = -EINVAL;
		goto fail;
	}

	evt->active.state = evl_shared_memory + bind.eids.state_offset;
	evt->active.fundle = bind.eids.fundle;
	evt->active.efd = efd;
	evt->magic = __EVENT_ACTIVE_MAGIC;

	return 0;
fail:
	close(efd);

	return ret;
}

int evl_new_event(struct evl_event *evt,
		int clockfd, const char *fmt, ...)
{
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = init_event_vargs(evt, clockfd, fmt, ap);
	va_end(ap);

	return efd;
}

int evl_open_event(struct evl_event *evt, const char *fmt, ...)
{
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = open_event_vargs(evt, fmt, ap);
	va_end(ap);

	return efd;
}

int evl_close_event(struct evl_event *evt)
{
	int efd;

	if (evt->magic == __EVENT_UNINIT_MAGIC)
		return 0;

	if (evt->magic != __EVENT_ACTIVE_MAGIC)
		return -EINVAL;

	efd = evt->active.efd;
	evt->active.efd = -1;
	compiler_barrier();
	close(efd);

	evt->active.fundle = EVL_NO_HANDLE;
	evt->active.state = NULL;
	evt->magic = __EVENT_DEAD_MAGIC;

	return 0;
}

static int check_event_sanity(struct evl_event *evt)
{
	int efd;

	if (evt->magic == __EVENT_UNINIT_MAGIC) {
		efd = init_event(evt, evt->uninit.clockfd, evt->uninit.name);
		if (efd < 0)
			return efd;
	} else if (evt->magic != __EVENT_ACTIVE_MAGIC)
		return -EINVAL;

	return 0;
}

static struct evl_monitor_state *get_lock_state(struct evl_event *evt)
{
	struct evl_monitor_state *est = evt->active.state;

	if (est->u.event.gate_offset == EVL_MONITOR_NOGATE)
		return NULL;	/* Nobody waits on @evt */

	return evl_shared_memory + est->u.event.gate_offset;
}

struct unwait_data {
	struct evl_monitor_unwaitreq ureq;
	int efd;
};

static void unwait_event(void *data)
{
	struct unwait_data *unwait = data;
	int ret;

	do
		ret = oob_ioctl(unwait->efd, EVL_MONIOC_UNWAIT,
				&unwait->ureq);
	while (ret && errno == EINTR);
}

int evl_timedwait_event(struct evl_event *evt,
			struct evl_mutex *mutex,
			const struct timespec *timeout)
{
	struct evl_monitor_waitreq req;
	struct unwait_data unwait;
	int ret;

	if (mutex->magic != __MUTEX_ACTIVE_MAGIC)
		return -EINVAL;

	ret = check_event_sanity(evt);
	if (ret)
		return ret;

	req.gatefd = mutex->active.efd;
	req.timeout = *timeout;
	req.status = -EINVAL;
	req.value = 0;		/* dummy */
	unwait.ureq.gatefd = req.gatefd;
	unwait.efd = evt->active.efd;

	pthread_cleanup_push(unwait_event, &unwait);
	ret = oob_ioctl(evt->active.efd, EVL_MONIOC_WAIT, &req);
	pthread_cleanup_pop(0);

	/*
	 * If oob_ioctl() failed with EINTR, we got forcibly unblocked
	 * while waiting for the event or trying to reacquire the lock
	 * afterwards, in which case the mutex was left
	 * unlocked. Issue UNWAIT to recover from that situation.
	 */
	if (ret && errno == EINTR) {
		unwait_event(&unwait);
		pthread_testcancel();
	}

	return ret ? -errno : req.status;
}

int evl_wait_event(struct evl_event *evt, struct evl_mutex *mutex)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedwait_event(evt, mutex, &timeout);
}

int evl_signal_event(struct evl_event *evt)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_event_sanity(evt);
	if (ret)
		return ret;

	gst = get_lock_state(evt);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = evt->active.state;
		est->flags |= EVL_MONITOR_SIGNALED;
	}

	return 0;
}

int evl_signal_thread(struct evl_event *evt, int thrfd)
{
	struct evl_monitor_state *gst;
	__u32 efd;
	int ret;

	ret = check_event_sanity(evt);
	if (ret)
		return ret;

	gst = get_lock_state(evt);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		efd = evt->active.efd;
		return oob_ioctl(thrfd, EVL_THRIOC_SIGNAL, &efd) ? -errno : 0;
	}

	/* No thread waits on @evt, so @thrfd neither => nop. */

	return 0;
}

int evl_broadcast_event(struct evl_event *evt)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_event_sanity(evt);
	if (ret)
		return ret;

	gst = get_lock_state(evt);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = evt->active.state;
		est->flags |= EVL_MONITOR_SIGNALED|EVL_MONITOR_BROADCAST;
	}

	return 0;
}
