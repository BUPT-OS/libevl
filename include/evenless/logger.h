/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_LOGGER_H
#define _EVENLESS_LOGGER_H

#include <sys/types.h>
#include <linux/types.h>
#include <uapi/evenless/logger.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_logger(int dstfd, size_t logsz);

ssize_t evl_log(int fd, const void *buf, size_t len);

int evl_printf(int fd, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_LOGGER_H */
