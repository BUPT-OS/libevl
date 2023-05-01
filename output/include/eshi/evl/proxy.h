/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_PROXY_H
#define _EVL_ESHI_PROXY_H

#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <evl/uapi.h>

#define evl_new_proxy(__targetfd, __bufsz, __fmt, __args...)	\
	evl_create_proxy(__targetfd, __bufsz, 0, 0, __fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

static inline
int evl_create_proxy(int fd, size_t bufsz, size_t granularity,
		int flags, const char *fmt, ...)
{
	return fd;
}

static inline
ssize_t evl_send_proxy(int proxyfd, const void *buf, size_t count)
{
	ssize_t ret = write(proxyfd, buf, count);
	return ret < 0 ? -errno : ret;
}

int evl_vprint_proxy(int proxyfd,
		const char *fmt, va_list ap);

int evl_print_proxy(int proxyfd,
		const char *fmt, ...);

#define evl_printf(__fmt, __args...)			\
	({						\
		int __ret = printf(__fmt, ##__args);	\
		__ret < 0 ? -errno : __ret;		\
	})

#define proxy_outfd  fileno(stdout)
#define	proxy_errfd  fileno(stderr)

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_PROXY_H */
