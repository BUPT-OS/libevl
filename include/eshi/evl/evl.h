/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_EVL_H
#define _EVL_ESHI_EVL_H

#include <evl/syscall.h>

#define __EVL__  7	/* API version */

struct evl_version {
	int api_level;	/* libevl.so: __EVL__ */
	int abi_level;	/* core: EVL_ABI_LEVEL, -1 for ESHI */
	const char *version_string;
};

#ifdef __cplusplus
extern "C" {
#endif

int evl_init(void);

struct evl_version evl_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_EVL_H */
