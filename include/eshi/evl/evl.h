/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_EVL_H
#define _EVL_ESHI_EVL_H

#include <evl/syscall.h>

#define __EVL__  5	/* API revision */

#ifdef __cplusplus
extern "C" {
#endif

int evl_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_EVL_H */
