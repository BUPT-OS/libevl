/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <evenless/syscall.h>
#include <evenless/utils.h>
#include "internal.h"

ssize_t evl_log(int fd, const void *buf, size_t len)
{
	if (evl_is_inband())
		return write(fd, buf, len);

	return oob_write(fd, buf, len);
}

int evl_printf(int fd, const char *fmt, ...)
{
	ssize_t len = EVL_PRINTBUF_SIZE;
	char *buf = evl_logging_buf;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf, len, fmt, ap);
	va_end(ap);

	return evl_log(fd, buf, len);
}
