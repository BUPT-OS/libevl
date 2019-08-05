/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_SYSCALL_H
#define _EVL_ESHI_SYSCALL_H

#include <sys/types.h>
#include <unistd.h>
#include <evl/uapi.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline ssize_t oob_read(int efd, void *buf, size_t count)
{
	return read(efd, buf, count);
}

static inline ssize_t oob_write(int efd, const void *buf, size_t count)
{
	return write(efd, buf, count);
}

#define oob_ioctl(__efd, __request, __args...)	\
	ioctl(__efd, __request, ##__args)

#ifdef __cplusplus
}
#endif

#endif /* _EVL_ESHI_SYSCALL_H */
