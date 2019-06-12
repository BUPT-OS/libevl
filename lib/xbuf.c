/*
 * SPDX-License-Identifier: MIT
 *
 * 2018 - Philippe Gerum <rpm@xenomai.org>
 */

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <evl/xbuf.h>
#include "internal.h"

int evl_new_xbuf(size_t i_bufsz, size_t o_bufsz,
		const char *fmt, ...)
{
	struct evl_xbuf_attrs attrs;
	int ret, efd;
	va_list ap;
	char *name;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	attrs.i_bufsz = i_bufsz;
	attrs.o_bufsz = o_bufsz;
	efd = create_evl_element(EVL_XBUF_DEV, name, &attrs, NULL);
	free(name);

	return efd;
}
