/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <evenless/syscall.h>
#include <evenless/poller.h>
#include "internal.h"

int evl_new_poller(int clockfd, const char *fmt, ...)
{
	struct evl_poller_attrs attrs;
	int ret, efd;
	va_list ap;
	char *name;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.clockfd = clockfd;
	efd = create_evl_element("poller", name, &attrs, NULL);
	free(name);

	return efd;
}

static int update_pollset(int efd, int op, int fd, unsigned int events)
{
	struct evl_poller_ctlreq creq;
	int ret;

	creq.action = op;
	creq.fd = fd;
	creq.events = events;
	ret = oob_ioctl(efd, EVL_POLIOC_CTL, &creq);

	return ret ? -errno : 0;
}

int evl_add_pollset(int efd, int fd, unsigned int events)
{
	return update_pollset(efd, EVL_POLLER_CTLADD, fd, events);
}

int evl_del_pollset(int efd, int fd)
{
	return update_pollset(efd, EVL_POLLER_CTLDEL, fd, 0);
}

int evl_mod_pollset(int efd, int fd, unsigned int events)
{
	return update_pollset(efd, EVL_POLLER_CTLMOD, fd, events);
}

static int do_poll_wait(int efd, struct evl_poll_event *pollset,
			int nrset, struct timespec *timeout)
{
	struct evl_poller_waitreq wreq;
	int ret;

	wreq.timeout = *timeout;
	wreq.pollset = pollset;
	wreq.nrset = nrset;
	ret = oob_ioctl(efd, EVL_POLIOC_WAIT, &wreq);
	if (ret)
		return -errno;

	return wreq.nrset;
}

int evl_wait_poller_timed(int efd, struct evl_poll_event *pollset,
			int nrset, struct timespec *timeout)
{
	return do_poll_wait(efd, pollset, nrset, timeout);
}

int evl_wait_poller(int efd, struct evl_poll_event *pollset, int nrset)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return do_poll_wait(efd, pollset, nrset, &timeout);
}
