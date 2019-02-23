/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_PROXY_H
#define _EVENLESS_PROXY_H

#include <sys/types.h>
#include <linux/types.h>
#include <uapi/evenless/proxy.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_proxy(int fd, size_t bufsz,
		const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_PROXY_H */
