/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <evl/syscall.h>
#include <evl/poll.h>
#include "internal.h"

int evl_new_poll(void)
{
	return create_evl_file("poll");
}

static int update_pollset(int efd, int op, int fd, unsigned int events)
{
	struct evl_poll_ctlreq creq;
	int ret;

	creq.action = op;
	creq.fd = fd;
	creq.events = events;
	ret = oob_ioctl(efd, EVL_POLIOC_CTL, &creq);

	return ret ? -errno : 0;
}

int evl_add_pollfd(int efd, int fd, unsigned int events)
{
	return update_pollset(efd, EVL_POLL_CTLADD, fd, events);
}

int evl_del_pollfd(int efd, int fd)
{
	return update_pollset(efd, EVL_POLL_CTLDEL, fd, 0);
}

int evl_mod_pollfd(int efd, int fd, unsigned int events)
{
	return update_pollset(efd, EVL_POLL_CTLMOD, fd, events);
}

static int do_poll(int efd, struct evl_poll_event *pollset,
		int nrset, struct timespec *timeout)
{
	struct evl_poll_waitreq wreq;
	int ret;

	wreq.timeout = *timeout;
	wreq.pollset = pollset;
	wreq.nrset = nrset;
	ret = oob_ioctl(efd, EVL_POLIOC_WAIT, &wreq);
	if (ret)
		return -errno;

	return wreq.nrset;
}

int evl_timedpoll(int efd, struct evl_poll_event *pollset,
		int nrset, struct timespec *timeout)
{
	return do_poll(efd, pollset, nrset, timeout);
}

int evl_poll(int efd, struct evl_poll_event *pollset, int nrset)
{
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };

	return do_poll(efd, pollset, nrset, &timeout);
}
