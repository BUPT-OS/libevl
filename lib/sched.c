/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <evl/thread.h>
#include <evl/syscall.h>
#include <evl/sched.h>
#include <uapi/evl/control.h>
#include "internal.h"

int evl_set_schedattr(int efd, const struct evl_sched_attrs *attrs)
{
	int ret;

	if (evl_is_inband())
		ret = ioctl(efd, EVL_THRIOC_SET_SCHEDPARAM, attrs);
	else
		ret = oob_ioctl(efd, EVL_THRIOC_SET_SCHEDPARAM, attrs);

	return ret ? -errno : 0;
}

int evl_get_schedattr(int efd, struct evl_sched_attrs *attrs)
{
	int ret;

	if (evl_is_inband())
		ret = ioctl(efd, EVL_THRIOC_GET_SCHEDPARAM, attrs);
	else
		ret = oob_ioctl(efd, EVL_THRIOC_GET_SCHEDPARAM, attrs);

	return ret ? -errno : 0;
}

int evl_sched_control(int policy,
		union evl_sched_ctlparam *param,
		union evl_sched_ctlinfo *info,
		int cpu)
{
	struct evl_sched_ctlreq ctlreq;
	int ret;

	if (evl_ctlfd < 0)
		return -ENXIO;

	ctlreq.policy = policy;
	ctlreq.cpu = cpu;
	ctlreq.param = param;
	ctlreq.info = info;

	if (evl_is_inband())
		ret = ioctl(evl_ctlfd, EVL_CTLIOC_SCHEDCTL, &ctlreq);
	else
		ret = oob_ioctl(evl_ctlfd, EVL_CTLIOC_SCHEDCTL, &ctlreq);

	return ret ? -errno : 0;
}
