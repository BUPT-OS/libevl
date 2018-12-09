/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_MAPPER_H
#define _EVENLESS_MAPPER_H

#include <sys/types.h>
#include <linux/types.h>
#include <uapi/evenless/mapper.h>

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_mapper(int mapfd, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_MAPPER_H */
