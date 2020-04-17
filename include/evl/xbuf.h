/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_XBUF_H
#define _EVL_XBUF_H

#include <sys/types.h>
#include <evl/syscall.h>
#include <linux/types.h>
#include <uapi/evl/xbuf.h>
#include <uapi/evl/factory.h>

#define evl_new_xbuf(__bufsz, __fmt, __args...)		     \
	evl_create_xbuf(__bufsz, __bufsz, EVL_CLONE_PRIVATE, \
			__fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

int evl_create_xbuf(size_t i_bufsz, size_t o_bufsz,
		int flags, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_XBUF_H */
