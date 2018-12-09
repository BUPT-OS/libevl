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
#include <evenless/mapper.h>
#include <evenless/syscall.h>
#include "internal.h"

int evl_new_mapper(int mapfd, const char *fmt, ...)
{
	struct evl_mapper_attrs attrs;
	int ret, efd;
	va_list ap;
	char *name;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;
	
	attrs.fd = mapfd;
	efd = create_evl_element("mapper", name, &attrs, NULL);
	free(name);

	return efd;
}
