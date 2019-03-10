/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_SYSCALL_H
#define _EVL_SYSCALL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t oob_read(int efd, void *buf, size_t count);

ssize_t oob_write(int efd, const void *buf, size_t count);

int oob_ioctl(int efd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_SYSCALL_H */
