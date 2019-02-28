/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <evenless/syscall.h>
#include <evenless/timer.h>
#include "internal.h"

int evl_new_timer(int clockfd, const char *fmt, ...)
{
	struct evl_timerfd_attrs attrs;
	int ret, efd;
	va_list ap;
	char *name;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.clockfd = clockfd;
	efd = create_evl_element("timer", name, &attrs, NULL);
	free(name);

	return efd;
}

int evl_set_timer(int efd,
		struct itimerspec *value,
		struct itimerspec *ovalue)
{
	struct evl_timerfd_setreq sreq;
	int ret;

	sreq.value = value;
	sreq.ovalue = ovalue;
	ret = oob_ioctl(efd, EVL_TFDIOC_SET, &sreq);

	return ret ? -errno : 0;
}

int evl_get_timer(int efd, struct itimerspec *value)
{
	struct evl_timerfd_getreq greq;
	int ret;

	greq.value = value;
	ret = oob_ioctl(efd, EVL_TFDIOC_GET, &greq);

	return ret ? -errno : 0;
}
