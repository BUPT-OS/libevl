/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _EVENLESS_TESTS_HELPERS_H
#define _EVENLESS_TESTS_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define warn_failed(__fmt, __args...)	\
	fprintf(stderr, "%s:%d: FAILED: " __fmt "\n", __FILE__, __LINE__, ##__args)

#define abort_test()	exit(1)

#define __Tcall(__ret, __call)				\
	({						\
		(__ret) = (__call);			\
		if (__ret < 0) {			\
			warn_failed("%s (=%s)",		\
				__stringify(__call),	\
				strerror(-(__ret)));	\
		}					\
		(__ret) >= 0;				\
	})

#define __Tcall_assert(__ret, __call)		\
	do {					\
		if (!__Tcall(__ret, __call))	\
			abort_test();		\
	} while (0)

#define __Fcall(__ret, __call)				\
	({						\
		(__ret) = (__call);			\
		if ((__ret) >= 0)			\
			warn_failed("%s (%d >= 0)",	\
				__stringify(__call),	\
				__ret);			\
		(__ret) < 0;				\
	})

#define __Fcall_assert(__ret, __call)		\
	do {					\
		if (!__Fcall(__ret, __call))	\
			abort_test();		\
	} while (0)

#define __Tcall_errno(__ret, __call)			\
	({						\
		(__ret) = (__call);			\
		if (__ret)				\
			warn_failed("%s (=%s)",		\
				__stringify(__call),	\
				strerror(errno));	\
		(__ret) == 0;				\
	})

#define __Tcall_errno_assert(__ret, __call)			\
	do {							\
		if (!__Tcall_errno(__ret, __call))		\
			abort_test();				\
	} while (0)

#define __Texpr(__expr)						\
	({							\
		int __ret = !!(__expr);				\
		if (!__ret)					\
			warn_failed("%s (=false)",		\
				__stringify(__expr));		\
		__ret;						\
	})

#define __Texpr_assert(__expr)		\
	do {				\
		if (!__Texpr(__expr))	\
			abort_test();	\
	} while (0)

#define __Fexpr(__expr)					\
	({						\
		int __ret = (__expr);			\
		if (__ret)				\
			warn_failed("%s (=true)",	\
				__stringify(__expr));	\
		!__ret;					\
	})

#define __Fexpr_assert(__expr)		\
	do {				\
		if (!__Fexpr(__expr))	\
			abort_test();	\
	} while (0)

char *get_unique_name_and_path(const char *type,
		int serial, char **ppath);

static inline char *get_unique_name(const char *type,
				int serial)
{
	return get_unique_name_and_path(type, serial, NULL);
}

int new_thread(pthread_t *tid, int policy, int prio,
	void *(*fn)(void *), void *arg);

void timespec_add_ns(struct timespec *__restrict r,
		const struct timespec *__restrict t,
		long ns);

#endif /* !_EVENLESS_TESTS_HELPERS_H */
