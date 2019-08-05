/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <evl/thread.h>
#include <evl/proxy.h>

static __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
char fmt_buf[1024];

int evl_vprint_proxy(int proxyfd, const char *fmt, va_list ap)
{
	ssize_t len = vsnprintf(fmt_buf, sizeof(fmt_buf), fmt, ap);

	return write(proxyfd, fmt_buf, len);
}

int evl_print_proxy(int proxyfd, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = evl_vprint_proxy(proxyfd, fmt, ap);
	va_end(ap);

	return ret;
}
