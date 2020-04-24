/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <evl/poll.h>
#include "internal.h"

int evl_new_poll(void)
{
	return epoll_create1(EPOLL_CLOEXEC);
}

int evl_add_pollfd(int efd, int newfd, unsigned int events,
	union evl_value pollval)
{
	struct epoll_event ev;
	int ret;

	if (efd == newfd)
		return -ELOOP;

	ev.events = events;
	ev.data.fd = newfd;

	ret = epoll_ctl(efd, EPOLL_CTL_ADD, newfd, &ev);
	if (ret)
		return -errno;

	return 0;
}

int evl_del_pollfd(int efd, int delfd)
{
	int ret;

	ret = epoll_ctl(efd, EPOLL_CTL_DEL, delfd, NULL);
	if (ret)
		return -errno;

	return 0;
}

int evl_mod_pollfd(int efd, int modfd, unsigned int events,
	union evl_value pollval)
{
	struct epoll_event ev;
	int ret;

	ev.events = events;
	ev.data.fd = modfd;

	ret = epoll_ctl(efd, EPOLL_CTL_MOD, modfd, &ev);
	if (ret)
		return -errno;

	return 0;
}

/*
 * Use a fast stack-based array for monitoring a small number of
 * events.
 */
#define FAST_EVENT_NR  8

static int do_timedpoll(int efd, struct evl_poll_event *pollset,
			int nrset, int msecs)
{
	struct epoll_event fast_evs[FAST_EVENT_NR], *evs = fast_evs;
	int ret, n;

	if (nrset == 0)
		return 0;

	if (nrset < 0)
		return -EINVAL;

	if (nrset > FAST_EVENT_NR) {
		evs = malloc(sizeof(*evs) * nrset);
		if (evs == NULL)
			return -ENOMEM;
	}

	ret = epoll_wait(efd, evs, nrset, msecs);
	if (ret < 0) {
		ret = -errno;
		goto out;
	}

	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto out;
	}

	for (n = 0; n < nrset; n++) {
		pollset[n].fd = evs[n].data.fd;
		pollset[n].events = evs[n].events;
	}
out:
	if (evs != fast_evs)
		free(evs);

	return ret;
}

int evl_timedpoll(int efd, struct evl_poll_event *pollset,
		int nrset, struct timespec *timeout)
{
	int msecs;

	if (timeout->tv_sec < 0 || timeout->tv_nsec >= 1000000000L)
		return -EINVAL;

	msecs = timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000;

	return do_timedpoll(efd, pollset, nrset, msecs);
}

int evl_poll(int efd, struct evl_poll_event *pollset, int nrset)
{
	return do_timedpoll(efd, pollset, nrset, -1);
}
