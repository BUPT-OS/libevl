/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <evenless/syscall.h>
#include <asm/evenless/syscall.h>
#include <uapi/evenless/syscall.h>

ssize_t oob_read(int efd, void *buf, size_t count)
{
	int old_type;
	ssize_t ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);
	ret = evenless_syscall3(sys_evenless_read, efd, buf, count);
	pthread_setcanceltype(old_type, NULL);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}

ssize_t oob_write(int efd, const void *buf, size_t count)
{
	int old_type;
	ssize_t ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);
	ret = evenless_syscall3(sys_evenless_write, efd, buf, count);
	pthread_setcanceltype(old_type, NULL);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}

int oob_ioctl(int efd, unsigned long request, ...)
{
	int ret, old_type;
	va_list ap;
	long arg;

	va_start(ap, request);
	arg = va_arg(ap, long);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);
	ret = evenless_syscall3(sys_evenless_ioctl, efd, request, arg);
	pthread_setcanceltype(old_type, NULL);
	va_end(ap);

	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}
