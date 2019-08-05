/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_PROXY_H
#define _EVL_ESHI_PROXY_H

#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <evl/uapi.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline
int evl_new_proxy(int fd, size_t bufsz, size_t granularity,
		const char *fmt, ...)
{
	return fd;
}

static inline
ssize_t evl_send_proxy(int proxyfd, const void *buf, size_t len)
{
	return write(proxyfd, buf, len);
}

int evl_vprint_proxy(int proxyfd,
		const char *fmt, va_list ap);

int evl_print_proxy(int proxyfd,
		const char *fmt, ...);

#define evl_printf(__fmt, __args...)	printf(__fmt, ##__args)

#define proxy_outfd  fileno(stdout)
#define	proxy_errfd  fileno(stderr)

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_PROXY_H */
