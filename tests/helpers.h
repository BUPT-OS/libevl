/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _EVENLESS_TESTS_HELPERS_H
#define _EVENLESS_TESTS_HELPERS_H

#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define warn_failed(__fmt, __args...)	\
	fprintf(stderr, "FAILED: " __fmt, ##__args)

#define __T(__ret, __action)					\
	({							\
		(__ret) = (__action);				\
		if (__ret < 0) {				\
			(__ret) = -(__ret);			\
			warn_failed("%s (=%s)",			\
				__stringify(__action),		\
				strerror(__ret));		\
		}						\
		(__ret) >= 0;					\
	})

#define __F(__ret, __action)					\
	({							\
		(__ret) = (__action);				\
		if ((__ret) >= 0)				\
			warn_failed("%s (%d >= 0)",		\
				__stringify(__action),		\
				__ret);				\
		(__ret) < 0;					\
	})

#define __Terrno(__ret, __action)				\
	({							\
		(__ret) = (__action);				\
		if (__ret)					\
			warn_failed("%s (=%s)",			\
				__stringify(__action),		\
				strerror(errno));		\
		(__ret) == 0;					\
	})

#define __Tassert(__expr)					\
	({							\
		int __ret = !!(__expr);				\
		if (!__ret)					\
			warn_failed("%s (=false)",		\
				__stringify(__expr));		\
		__ret;						\
	})

#define __Fassert(__expr)					\
	({							\
		int __ret = (__expr);				\
		if (__ret)					\
			warn_failed("%s (=true)",		\
				__stringify(__expr));		\
		!__ret;						\
	})

static inline
char *get_unique_name(const char *type, int serial, char **ppath)
{
	int ret;

	ret = asprintf(ppath, "/dev/evenless/%s/test%d.%d",
		       type, getpid(), serial);
	if (ret < 0)
		error(1, ENOMEM, "malloc");

	return strrchr(*ppath, '/') + 1;
}

#endif /* !_EVENLESS_TESTS_HELPERS_H */
