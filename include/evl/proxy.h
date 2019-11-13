/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_PROXY_H
#define _EVL_PROXY_H

#include <stdarg.h>
#include <sys/types.h>
#include <linux/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_proxy(int targetfd, size_t bufsz,
		size_t granularity,
		const char *fmt, ...);

ssize_t evl_send_proxy(int proxyfd,
		const void *buf, size_t count);

int evl_vprint_proxy(int proxyfd,
		const char *fmt, va_list ap);

int evl_print_proxy(int proxyfd,
		const char *fmt, ...);

int evl_printf(const char *fmt, ...);

extern int proxy_outfd,
	proxy_errfd;

#ifdef __cplusplus
}
#endif

#endif /* _EVL_PROXY_H */
