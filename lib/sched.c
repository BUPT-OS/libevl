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

#define do_call(__fd, __args...)				\
	({							\
		int __ret;					\
		if (evl_is_inband())				\
			__ret = ioctl(__fd, ##__args);		\
		else						\
			__ret = oob_ioctl(__fd, ##__args);	\
		__ret ? -errno : 0;				\
	})

int evl_set_schedattr(int efd, const struct evl_sched_attrs *attrs)
{
	return do_call(efd, EVL_THRIOC_SET_SCHEDPARAM, attrs);
}

int evl_get_schedattr(int efd, struct evl_sched_attrs *attrs)
{
	return do_call(efd, EVL_THRIOC_GET_SCHEDPARAM, attrs);
}

int evl_sched_control(int policy,
		const union evl_sched_ctlparam *param,
		union evl_sched_ctlinfo *info,
		int cpu)
{
	struct evl_sched_ctlreq ctlreq;

	if (evl_ctlfd < 0)
		return -ENXIO;

	ctlreq.policy = policy;
	ctlreq.cpu = cpu;
	ctlreq.param = param;
	ctlreq.info = info;

	return do_call(evl_ctlfd, EVL_CTLIOC_SCHEDCTL, &ctlreq);
}

int evl_get_cpustate(int cpu, int *state_r)
{
	struct evl_cpu_state cpst;
	__u32 state;
	int ret;

	if (evl_ctlfd < 0)
		return -ENXIO;

	cpst.cpu = cpu;
	cpst.state = &state;

	ret = do_call(evl_ctlfd, EVL_CTLIOC_GET_CPUSTATE, &cpst);
	if (ret)
		return ret;

	*state_r = (int)state;

	return 0;
}
