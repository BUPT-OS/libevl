/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_SCHED_H
#define _EVL_ESHI_SCHED_H

struct evl_sched_attrs {
	int sched_policy;
	int sched_priority;
};

#ifdef __cplusplus
extern "C" {
#endif

int evl_set_schedattr(int efd, const struct evl_sched_attrs *attrs);

int evl_get_schedattr(int efd, struct evl_sched_attrs *attrs);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_SCHED_H */
