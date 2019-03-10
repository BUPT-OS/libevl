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
#include <evl/syscall.h>
#include "internal.h"

int evl_new_proxy(int fd, size_t bufsz, const char *fmt, ...)
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

	attrs.fd = fd;
	attrs.bufsz = bufsz;
	efd = create_evl_element("proxy", name, &attrs, NULL);
	free(name);

	return efd;
}
