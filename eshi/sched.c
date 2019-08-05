/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <sched.h>
#include <evl/sched.h>
#include "internal.h"

int evl_set_schedattr(int efd, const struct evl_sched_attrs *attrs)
{
	int policy = attrs->sched_policy;
	struct sched_param param;
	pthread_t thread;

	if (policy != SCHED_OTHER && policy != SCHED_FIFO)
		return -EINVAL;

	thread = eshi_find_thread_by_fd(efd);
	if (!thread)
		return -EBADF;

	param.sched_priority = attrs->sched_priority;

	return -pthread_setschedparam(thread, policy, &param);
}

int evl_get_schedattr(int efd, struct evl_sched_attrs *attrs)
{
	int policy = attrs->sched_policy;
	struct sched_param param;
	pthread_t thread;
	int ret;

	thread = eshi_find_thread_by_fd(efd);
	if (!thread)
		return -EBADF;

	ret = pthread_getschedparam(thread, &policy, &param);
	if (ret)
		return -ret;

	attrs->sched_policy = policy;
	attrs->sched_priority = param.sched_priority;

	return 0;
}
