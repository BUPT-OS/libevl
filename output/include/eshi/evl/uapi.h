/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_UAPI_H
#define _EVL_ESHI_UAPI_H

#include <stdint.h>

#define EVL_CLOCK_MONOTONIC_DEV	"monotonic"
#define EVL_CLOCK_REALTIME_DEV	"realtime"
#define EVL_CLOCK_DEV		"clock"
#define EVL_MONITOR_DEV		"monitor"
#define EVL_POLL_DEV		"poll"
#define EVL_PROXY_DEV		"proxy"
#define EVL_THREAD_DEV		"thread"
#define EVL_XBUF_DEV		"xbuf"
#define EVL_OBSERVABLE_DEV	"observable"

union evl_value {
	int32_t val;
	int64_t lval;
	void *ptr;
};

#define evl_nil  ((union evl_value){ .lval = 0 })

#endif /* _EVL_ESHI_UAPI_H */
