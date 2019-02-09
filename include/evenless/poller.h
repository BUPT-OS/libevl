/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_POLLER_H
#define _EVENLESS_POLLER_H

#include <sys/types.h>
#include <linux/types.h>
#include <uapi/evenless/poller.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_poller(int clockfd,
		const char *fmt, ...);

int evl_add_pollset(int efd, int newfd,
		unsigned int events);

int evl_del_pollset(int efd, int delfd);

int evl_mod_pollset(int efd, int modfd,
		unsigned int events);

int evl_wait_poller_timed(int efd, struct evl_poll_event *pollset,
			int nrset, struct timespec *timeout);

int evl_wait_poller(int efd, struct evl_poll_event *pollset,
		int nrset);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_POLLER_H */
