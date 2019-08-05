/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_POLL_H
#define _EVL_ESHI_POLL_H

#include <linux/types.h>
#include <sys/poll.h>
#include <evl/uapi.h>

struct evl_poll_event {
	__u32 fd;
	__u32 events;
};

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_poll(void);

int evl_add_pollfd(int efd, int newfd,
		unsigned int events);

int evl_del_pollfd(int efd, int delfd);

int evl_mod_pollfd(int efd, int modfd,
		unsigned int events);

int evl_timedpoll(int efd, struct evl_poll_event *pollset,
		int nrset, struct timespec *timeout);

int evl_poll(int efd, struct evl_poll_event *pollset,
	int nrset);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_POLL_H */
