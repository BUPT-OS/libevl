/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <evl/proxy.h>
#include <evl/thread.h>
#include <evl/syscall.h>
#include <uapi/evl/proxy.h>
#include "internal.h"

#define STDSTREAM_BUFSZ  16384

static __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
char fmt_buf[1024];

int proxy_outfd = -EBADF, proxy_errfd = -EBADF;

void init_proxy_streams(void)
{
	/*
	 * This might fail if stdout/stderr are closed, just ignore if
	 * so.
	 */
	proxy_outfd = evl_new_proxy(fileno(stdout), STDSTREAM_BUFSZ, 0,
		"stdout:%d", getpid());
	proxy_errfd = evl_new_proxy(fileno(stderr), STDSTREAM_BUFSZ, 0,
		"stderr:%d", getpid());
}

int evl_new_proxy(int targetfd, size_t bufsz, size_t granularity,
		const char *fmt, ...)
{
	struct evl_proxy_attrs attrs;
	int ret, efd;
	va_list ap;
	char *name;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.fd = targetfd;
	attrs.bufsz = bufsz;
	attrs.granularity = granularity;
	efd = create_evl_element(EVL_PROXY_DEV, name, &attrs, NULL);
	free(name);

	return efd;
}

ssize_t evl_send_proxy(int proxyfd, const void *buf, size_t count)
{
	ssize_t ret;

	if (evl_is_inband())
		ret = write(proxyfd, buf, count);
	else
		ret = oob_write(proxyfd, buf, count);

	return ret < 0 ? -errno : ret;
}

int evl_vprint_proxy(int proxyfd, const char *fmt, va_list ap)
{
	ssize_t count = vsnprintf(fmt_buf, sizeof(fmt_buf), fmt, ap);

	if (count < 0)
		return -errno;	/* assume POSIX behavior. */

	return (int)evl_send_proxy(proxyfd, fmt_buf, count);
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

int evl_printf(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);

	/*
	 * If evl_init() has not run yet, we can resort to vprintf()
	 * since latency is not an issue.
	 */
	if (evl_ctlfd < 0) {
		ret = vprintf(fmt, ap);
		if (ret < 0)
			ret = -errno;
	} else
		ret = evl_vprint_proxy(proxy_outfd, fmt, ap);

	va_end(ap);

	return ret;
}
