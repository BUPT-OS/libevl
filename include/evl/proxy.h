/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_PROXY_H
#define _EVL_PROXY_H

#include <sys/types.h>
#include <linux/types.h>
#include <uapi/evl/proxy.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_proxy(int fd, size_t bufsz,
		size_t granularity,
		const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_PROXY_H */
