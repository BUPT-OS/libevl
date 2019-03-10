/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_UTILS_H
#define _EVL_UTILS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t evl_log(int fd, const void *buf, size_t len);

int evl_printf(int fd, const char *fmt, ...);

int evl_sched_control(int policy,
		union evl_sched_ctlparam *param,
		union evl_sched_ctlinfo *info,
		int cpu);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_UTILS_H */
