/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_OBSERVABLE_H
#define _EVL_OBSERVABLE_H

#include <time.h>
#include <stdint.h>
#include <evl/syscall.h>
#include <uapi/evl/observable.h>
#include <uapi/evl/factory.h>

struct evl_notification {
	uint32_t tag;
	uint32_t serial;
	int32_t issuer;
	union evl_value event;
	struct timespec date;
};

#define evl_new_observable(__fmt, __args...)	\
	evl_create_observable(EVL_CLONE_PRIVATE, __fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

int evl_create_observable(int flags, const char *fmt, ...);

int evl_update_observable(int ofd, const struct evl_notice *ntc,
			int nr);

int evl_read_observable(int ofd, struct evl_notification *nf,
			int nr);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_OBSERVABLE_H */
