/*
 * SPDX-License-Identifier: MIT
 *
 * 2018 - Philippe Gerum <rpm@xenomai.org>
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <evl/xbuf.h>
#include "internal.h"

int evl_create_xbuf(size_t i_bufsz, size_t o_bufsz,
		int flags, const char *fmt, ...)
{
	printf("xbuf11111\n");
	struct evl_xbuf_attrs attrs;
	char *name = NULL;
	int ret, efd;
	va_list ap;

	if (fmt) {
		va_start(ap, fmt);
		ret = vasprintf(&name, fmt, ap);
		va_end(ap);
		if (ret < 0)
			return -ENOMEM;
	}

	// printf("xbuf11111");
	attrs.i_bufsz = i_bufsz;
	attrs.o_bufsz = o_bufsz;
	printf("xbuf-name:%s\n", name);
	efd = create_evl_element(EVL_XBUF_DEV, name, &attrs, flags, NULL);
	printf("xbuf-name:%s\n", name);
	printf("efd of xbuf is %d\n", efd);
	if (name)
		free(name);

	return efd;
}
