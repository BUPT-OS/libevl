/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_MONITOR_H
#define _EVENLESS_MONITOR_H

#include <time.h>
#include <linux/types.h>
#include <evenless/atomic.h>
#include <uapi/evenless/types.h>
#include <uapi/evenless/monitor.h>

struct evl_monitor {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_monitor_state *state;
			int efd;
			int type;
		} active;
		struct {
			const char *name;
			int clockfd;
			unsigned int ceiling;
			int type;
		} uninit;
	};
};

#define __MONITOR_ACTIVE_MAGIC  0xab12ab12
#define __MONITOR_UNINIT_MAGIC  0xfe11fe11
#define __MONITOR_DEAD_MAGIC	0

#endif /* _EVENLESS_MONITOR_H */
