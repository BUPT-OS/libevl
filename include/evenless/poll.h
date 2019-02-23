/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_POLL_H
#define _EVENLESS_POLL_H

#include <sys/types.h>
#include <linux/types.h>
#include <uapi/evenless/poller.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_poll(const char *fmt, ...);

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

#endif /* _EVENLESS_POLL_H */
