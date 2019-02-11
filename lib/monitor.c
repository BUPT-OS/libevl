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
#include <evenless/thread.h>
#include <evenless/syscall.h>
#include <linux/types.h>
#include <uapi/evenless/factory.h>
#include <uapi/evenless/mutex.h>
#include "internal.h"

static int init_monitor_vargs(struct evl_monitor *mon,
			int type, int clockfd, unsigned int ceiling,
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
	 * the co-kernel does not require this, a 1:1 mapping between
	 * in-band and co-kernel priorities is simpler to deal with
	 * for users with respect to inband <-> out-of-band mode
	 * switches.
	 */
	if (type == EVL_MONITOR_PP) {
		ret = sched_get_priority_max(SCHED_FIFO);
		if (ret < 0 || ceiling > ret)
			return -EINVAL;
	}

	ret = vasprintf(&name, fmt, ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.type = type;
	attrs.clockfd = clockfd;
	attrs.ceiling = ceiling;
	efd = create_evl_element("monitor", name, &attrs, &eids);
	free(name);
	if (efd < 0)
		return efd;

	mon->active.state = evl_shared_memory + eids.state_offset;
	mon->active.fundle = eids.fundle;
	mon->active.type = type;
	mon->active.efd = efd;
	mon->magic = __MONITOR_ACTIVE_MAGIC;

	return 0;
}

static int init_monitor(struct evl_monitor *mon,
			int type, int clockfd, unsigned int ceiling,
			const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_monitor_vargs(mon, type, clockfd, ceiling, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_new_gate(struct evl_monitor *gate, int clockfd,
		 const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_monitor_vargs(gate, EVL_MONITOR_PI, clockfd, 0, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_new_gate_ceiling(struct evl_monitor *gate, int clockfd,
			unsigned int ceiling,
			const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_monitor_vargs(gate, EVL_MONITOR_PP, clockfd, ceiling, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_new_event(struct evl_monitor *event,
		  int clockfd, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = init_monitor_vargs(event, EVL_MONITOR_EV, clockfd, 0, fmt, ap);
	va_end(ap);

	return ret;
}

int evl_open_monitor(struct evl_monitor *mon, const char *fmt, ...)
{
	struct evl_monitor_binding bind;
	int ret, efd;
	va_list ap;

	va_start(ap, fmt);
	efd = open_evl_element_vargs("monitor", fmt, ap);
	va_end(ap);
	if (efd < 0)
		return efd;

	ret = ioctl(efd, EVL_MONIOC_BIND, &bind);
	if (ret)
		return -errno;

	mon->active.state = evl_shared_memory + bind.eids.state_offset;
	mon->active.fundle = bind.eids.fundle;
	mon->active.type = bind.type;
	mon->active.efd = efd;
	mon->magic = __MONITOR_ACTIVE_MAGIC;

	return 0;
}

int evl_release_monitor(struct evl_monitor *mon)
{
	int ret;

	if (mon->magic != __MONITOR_ACTIVE_MAGIC)
		return -EINVAL;

	ret = close(mon->active.efd);
	if (ret)
		return -errno;

	mon->active.fundle = EVL_NO_HANDLE;
	mon->active.state = NULL;
	mon->magic = __MONITOR_DEAD_MAGIC;

	return 0;
}

static int try_enter_gate(struct evl_monitor *gate)
{
	struct evl_user_window *u_window;
	struct evl_monitor_state *gst;
	bool protect = false;
	fundle_t current;
	int mode, ret;

	current = evl_get_current();
	if (current == EVL_NO_HANDLE)
		return -EPERM;

	if (gate->magic == __MONITOR_UNINIT_MAGIC &&
	    gate->uninit.type != EVL_MONITOR_EV) {
		ret = init_monitor(gate,
				   gate->uninit.type,
				   gate->uninit.clockfd,
				   gate->uninit.ceiling,
				   gate->uninit.name);
		if (ret)
			return ret;
	} else if (gate->magic != __MONITOR_ACTIVE_MAGIC)
		return -EINVAL;

	gst = gate->active.state;

	/*
	 * Threads running in-band and/or enabling some debug features
	 * must go through the slow syscall path.
	 */
	mode = evl_get_current_mode();
	if (!(mode & (T_INBAND|T_WEAK|T_DEBUG))) {
		if (gate->active.type == EVL_MONITOR_PP) {
			u_window = evl_get_current_window();
			/*
			 * Can't nest lazy ceiling requests, have to
			 * take the slow path when this happens.
			 */
			if (u_window->pp_pending != EVL_NO_HANDLE)
				goto slow_path;
			u_window->pp_pending = gate->active.fundle;
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

int evl_enter_gate_timed(struct evl_monitor *gate,
			const struct timespec *timeout)
{
	struct evl_monitor_lockreq lreq;
	int ret, cancel_type;

	ret = try_enter_gate(gate);
	if (ret != -ENODATA)
		return ret;

	lreq.timeout = *timeout;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &cancel_type);

	do
		ret = oob_ioctl(gate->active.efd, EVL_MONIOC_ENTER, &lreq);
	while (ret && errno == EINTR);

	pthread_setcanceltype(cancel_type, NULL);

	return ret ? -errno : 0;
}

int evl_enter_gate(struct evl_monitor *gate)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_enter_gate_timed(gate, &timeout);
}

int evl_tryenter_gate(struct evl_monitor *gate)
{
	int ret;

	ret = try_enter_gate(gate);
	if (ret != -ENODATA)
		return ret;

	do
		ret = oob_ioctl(gate->active.efd, EVL_MONIOC_TRYENTER);
	while (ret && errno == EINTR);

	return ret ? -errno : 0;
}

int evl_exit_gate(struct evl_monitor *gate)
{
	struct evl_user_window *u_window;
	struct evl_monitor_state *gst;
	fundle_t current;
	int ret, mode;

	if (gate->magic != __MONITOR_ACTIVE_MAGIC)
		return -EINVAL;

	gst = gate->active.state;
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
		if (gate->active.type == EVL_MONITOR_PP) {
			u_window = evl_get_current_window();
			u_window->pp_pending = EVL_NO_HANDLE;
		}
		return 0;
	}

	/*
	 * If the fast release failed, somebody else must be waiting
	 * for entering the gate or PP was committed for the current
	 * thread. Need to ask the kernel for proper release.
	 */
slow_path:
	ret = oob_ioctl(gate->active.efd, EVL_MONIOC_EXIT);

	return ret ? -errno : 0;
}

int evl_set_gate_ceiling(struct evl_monitor *gate,
			 unsigned int ceiling)
{
	int ret;

	if (ceiling == 0)
		return -EINVAL;

	ret = sched_get_priority_max(SCHED_FIFO);
	if (ret < 0 || ceiling > ret)
		return -EINVAL;

	if (gate->magic == __MONITOR_UNINIT_MAGIC) {
		if (gate->uninit.type != EVL_MONITOR_PP)
			return -EINVAL;
		gate->uninit.ceiling = ceiling;
		return 0;
	}

	if (gate->magic != __MONITOR_ACTIVE_MAGIC ||
	    gate->active.type != EVL_MONITOR_PP)
		return -EINVAL;

	gate->active.state->u.gate.ceiling = ceiling;

	return 0;
}

int evl_get_gate_ceiling(struct evl_monitor *gate)
{
	if (gate->magic == __MONITOR_UNINIT_MAGIC) {
		if (gate->uninit.type != EVL_MONITOR_PP)
			return -EINVAL;

		return gate->uninit.ceiling;
	}

	if (gate->magic != __MONITOR_ACTIVE_MAGIC ||
	    gate->active.type != EVL_MONITOR_PP)
		return -EINVAL;

	return gate->active.state->u.gate.ceiling;
}

static int check_event_sanity(struct evl_monitor *event)
{
	int ret;

	if (event->magic == __MONITOR_UNINIT_MAGIC &&
	    event->uninit.type == EVL_MONITOR_EV) {
		ret = evl_new_event(event,
				    event->uninit.clockfd,
				    event->uninit.name);
		if (ret)
			return ret;
	} else if (event->magic != __MONITOR_ACTIVE_MAGIC)
		return -EINVAL;

	if (event->active.type != EVL_MONITOR_EV)
		return -EINVAL;

	return 0;
}

static struct evl_monitor_state *
get_gate_state(struct evl_monitor *event)
{
	struct evl_monitor_state *est = event->active.state;

	if (est->u.gate_offset == EVL_MONITOR_NOGATE)
		return NULL;	/* Nobody waits on @event */

	return evl_shared_memory + est->u.gate_offset;
}

struct unwait_data {
	struct evl_monitor_unwaitreq ureq;
	int efd;
};

static void unwait_monitor(void *data)
{
	struct unwait_data *unwait = data;
	int ret;

	do
		ret = oob_ioctl(unwait->efd, EVL_MONIOC_UNWAIT,
				&unwait->ureq);
	while (ret && errno == EINTR);
}

int evl_wait_event_timed(struct evl_monitor *event,
			struct evl_monitor *gate,
			const struct timespec *timeout)
{
	struct evl_monitor_waitreq req;
	struct unwait_data unwait;
	int ret, old_type;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	req.gatefd = gate->active.efd;
	req.timeout = *timeout;
	req.status = -EINVAL;
	unwait.ureq.gatefd = gate->active.efd;
	unwait.efd = event->active.efd;

	pthread_cleanup_push(unwait_monitor, &unwait);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);
	ret = oob_ioctl(event->active.efd, EVL_MONIOC_WAIT, &req);
	pthread_setcanceltype(old_type, NULL);
	pthread_cleanup_pop(0);

	/*
	 * If oob_ioctl() failed with EINTR, we got forcibly unblocked
	 * while waiting for the event or trying to reacquire the gate
	 * lock afterwards, in which case the gate monitor was left
	 * unlocked. Issue UNWAIT to recover from that situation.
	 */
	if (ret && errno == EINTR) {
		unwait_monitor(&unwait);
		pthread_testcancel();
	}

	return ret ? -errno : req.status;
}

int evl_wait_event(struct evl_monitor *event,
		struct evl_monitor *gate)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return evl_wait_event_timed(event, gate, &timeout);
}

int evl_signal_event(struct evl_monitor *event)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	gst = get_gate_state(event);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = event->active.state;
		est->flags |= EVL_MONITOR_SIGNALED;
	}

	return 0;
}

int evl_signal_event_targeted(struct evl_monitor *event, int thrfd)
{
	struct evl_monitor_state *gst;
	__u32 efd;
	int ret;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	gst = get_gate_state(event);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		efd = event->active.efd;
	}

	return oob_ioctl(thrfd, EVL_THRIOC_SIGNAL, &efd) ? -errno : 0;
}

int evl_broadcast_event(struct evl_monitor *event)
{
	struct evl_monitor_state *est, *gst;
	int ret;

	ret = check_event_sanity(event);
	if (ret)
		return ret;

	gst = get_gate_state(event);
	if (gst) {
		gst->flags |= EVL_MONITOR_SIGNALED;
		est = event->active.state;
		est->flags |= EVL_MONITOR_SIGNALED|EVL_MONITOR_BROADCAST;
	}

	return 0;
}
