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
#include <evenless/atomic.h>
#include <evenless/evl.h>
#include <evenless/monitor.h>
#include <evenless/mutex.h>
#include <evenless/event.h>
#include <evenless/thread.h>
#include <evenless/syscall.h>
#include <linux/types.h>
#include <uapi/evenless/factory.h>
#include <uapi/evenless/mutex.h>
#include "internal.h"

#define __EVENT_ACTIVE_MAGIC	0xef55ef55
#define __EVENT_DEAD_MAGIC	0

static int init_event_vargs(struct evl_monitor *mon,
			int clockfd, unsigned int ceiling,
			const char *fmt, va_list ap)
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
	attrs.initval = ceiling;
	efd = create_evl_element("monitor", name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	mon->active.state = evl_shared_memory + eids.state_offset;
	mon->active.fundle = eids.fundle;
	mon->active.type = EVL_MONITOR_EV;
	mon->active.efd = efd;
	mon->magic = __EVENT_ACTIVE_MAGIC;

	return 0;
}

static int init_event(struct evl_monitor *mon,
		int clockfd, unsigned int ceiling,
		const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_event_vargs(mon, clockfd, ceiling, fmt, ap);
	va_end(ap);

	return ret;
}

static int open_event_vargs(struct evl_monitor *mon,
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

	mon->active.state = evl_shared_memory + bind.eids.state_offset;
	mon->active.fundle = bind.eids.fundle;
	mon->active.type = EVL_MONITOR_EV;
	mon->active.efd = efd;
	mon->magic = __EVENT_ACTIVE_MAGIC;

	return 0;
fail:
	close(efd);

	return ret;
}

int evl_new_event(struct evl_event *event,
		int clockfd, const char *fmt, ...)
{
	struct evl_monitor *_event = &event->__event;
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_event_vargs(_event, clockfd, 0, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_open_event(struct evl_event *event, const char *fmt, ...)
{
	struct evl_monitor *_event = &event->__event;
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = open_event_vargs(_event, fmt, ap);
	va_end(ap);

	return efd;
}

int evl_close_event(struct evl_event *event)
{
	struct evl_monitor *mon = &event->__event;
	int efd;

	if (mon->magic != __EVENT_ACTIVE_MAGIC)
		return -EINVAL;

	efd = mon->active.efd;
	mon->active.efd = -1;
	compiler_barrier();
	close(efd);

	mon->active.fundle = EVL_NO_HANDLE;
	mon->active.state = NULL;
	mon->magic = __EVENT_DEAD_MAGIC;

	return 0;
}

static int check_event_sanity(struct evl_event *event)
{
	struct evl_monitor *_event = &event->__event;
	int ret;

	if (_event->magic == __EVENT_UNINIT_MAGIC &&
		_event->uninit.type == EVL_MONITOR_EV) {
		ret = init_event(_event,
				_event->uninit.clockfd, 0,
				_event->uninit.name);
		if (ret)
			return ret;
	} else if (_event->magic != __EVENT_ACTIVE_MAGIC)
		return -EINVAL;

	if (_event->active.type != EVL_MONITOR_EV)
		return -EINVAL;

	return 0;
}

static struct evl_monitor_state *
get_lock_state(struct evl_event *event)
{
	struct evl_monitor_state *est = event->__event.active.state;

	if (est->u.event.gate_offset == EVL_MONITOR_NOGATE)
		return NULL;	/* Nobody waits on @event */

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

int evl_timedwait(struct evl_event *event,
		struct evl_mutex *mutex,
		const struct timespec *timeout)
{
	struct evl_monitor *_event = &event->__event;
	struct evl_monitor_waitreq req;
	struct unwait_data unwait;
	int ret, old_type;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	req.gatefd = mutex->__mutex.active.efd;
	req.timeout = *timeout;
	req.status = -EINVAL;
	unwait.ureq.gatefd = req.gatefd;
	unwait.efd = _event->active.efd;

	pthread_cleanup_push(unwait_event, &unwait);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);
	ret = oob_ioctl(_event->active.efd, EVL_MONIOC_WAIT, &req);
	pthread_setcanceltype(old_type, NULL);
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

int evl_wait(struct evl_event *event, struct evl_mutex *mutex)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_timedwait(event, mutex, &timeout);
}

int evl_signal(struct evl_event *event)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	gst = get_lock_state(event);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = event->__event.active.state;
		est->flags |= EVL_MONITOR_SIGNALED;
	}

	return 0;
}

int evl_signal_thread(struct evl_event *event, int thrfd)
{
	struct evl_monitor_state *gst;
	__u32 efd;
	int ret;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	gst = get_lock_state(event);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		efd = event->__event.active.efd;
	}

	return oob_ioctl(thrfd, EVL_THRIOC_SIGNAL, &efd) ? -errno : 0;
}

int evl_broadcast(struct evl_event *event)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	gst = get_lock_state(event);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = event->__event.active.state;
		est->flags |= EVL_MONITOR_SIGNALED|EVL_MONITOR_BROADCAST;
	}

	return 0;
}
