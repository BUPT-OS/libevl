/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <evenless/logger.h>
#include <evenless/syscall.h>
#include "internal.h"

int evl_new_logger(int dstfd, size_t logsz)
{
	struct evl_logger_attrs attrs;
	int fd, ret;

	fd = create_evl_file("logger");
	if (fd < 0)
		return fd;

	attrs.fd = dstfd;
	attrs.logsz = logsz;
	ret = ioctl(fd, EVL_LOGIOC_CONFIG, &attrs);
	if (ret) {
		ret = -errno;
		close(fd);
		return ret;
	}

	return fd;
}

ssize_t evl_write_logger(int fd, const void *buf, size_t len)
{
	if (evl_is_inband())
		return write(fd, buf, len);

	return oob_write(fd, buf, len);
}

int evl_printf_logger(int fd, const char *fmt, ...)
{
	ssize_t len = EVL_PRINTBUF_SIZE;
	char *buf = evl_logging_buf;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf, len, fmt, ap);
	va_end(ap);

	return evl_write_logger(fd, buf, len);
}
