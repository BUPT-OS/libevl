/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_UTILS_H
#define _EVENLESS_UTILS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t evl_log(int fd, const void *buf, size_t len);

int evl_printf(int fd, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_UTILS_H */