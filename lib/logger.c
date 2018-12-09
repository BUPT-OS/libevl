/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <evenless/logger.h>
#include <evenless/syscall.h>
#include "internal.h"

int evl_new_logger(int dstfd, size_t logsz, const char *fmt, ...)
{
	struct evl_logger_attrs attrs;
	int ret, efd;
	va_list ap;
	char *name;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.fd = dstfd;
	attrs.logsz = logsz;
	efd = create_evl_element("logger", name, &attrs, NULL);
	free(name);

	return efd;
}

ssize_t evl_write_logger(int efd, const void *buf, size_t len)
{
	if (evl_is_inband())
		return write(efd, buf, len);

	return oob_write(efd, buf, len);
}

int evl_printf_logger(int efd, const char *fmt, ...)
{
	ssize_t len = EVL_PRINTBUF_SIZE;
	char *buf = evl_logging_buf;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf, len, fmt, ap);
	va_end(ap);

	return evl_write_logger(efd, buf, len);
}
