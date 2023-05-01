/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_TIMER_H
#define _EVL_TIMER_H

#include <time.h>
#include <linux/types.h>
#include <uapi/evl/clock.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_timer(int clockfd);

int evl_set_timer(int efd,
		struct itimerspec *value,
		struct itimerspec *ovalue);

int evl_get_timer(int efd,
		struct itimerspec *value);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_TIMER_H */
