/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _LIB_EVL_INTERNAL_H
#define _LIB_EVL_INTERNAL_H

#include <stdarg.h>
#include <time.h>
#include <stdint.h>
#include <evl/thread.h>
#include <uapi/evl/types.h>

#define __evl_ptr64(__ptr)	((__u64)(uintptr_t)(__ptr))

#if __WORDSIZE == 64 || __TIMESIZE == 64
/*
 * If timespec is y2038-safe, we don't need to bounce via an
 * __evl_timespec buffer, since both types are guaranteed compatible
 * bitwise. y2038-proof glibc sets __TIMESIZE to the architecture
 * bitness, others (including earlier releases) might not so we check
 * __WORDSIZE too.
 *
 * CAUTION: the assumption here is that a y2038-safe timespec type has
 * to be compatible bitwise with __kernel_timespec, and __evl_timespec
 * is guaranteed compatible bitwise with __kernel_timespec too. So we
 * may cast values safely between these types.
 */
#define __evl_ktimespec(__ts, __kts)			\
	({						\
		(void)__kts;				\
		(struct __evl_timespec *)__ts;		\
	})
#define __evl_kitimerspec(__its, __kits)		\
	({						\
		(void)__kits;				\
		(struct __evl_itimerspec *)__its;	\
	})
#else
/*
 * Bummer, we have to bounce the 32bit timespec to a 64bit one the
 * kernel would accept. Downside of it: we might get SIGSEGV if __ts
 * is an invalid pointer instead of -EFAULT as one would
 * expect. Upside: this interface won't break user code which did not
 * switch to timespec64, which would be the only reasonable thing to
 * do when support for y2038 is generally available from *libc.
 */
#define __evl_ktimespec(__ts, __kts)			\
	({						\
		__kts.tv_sec = (__ts)->tv_sec;		\
		__kts.tv_nsec = (__ts)->tv_nsec;	\
		&__kts;					\
	})
#define __evl_kitimerspec(__its, __kits)				\
	({								\
		struct __evl_itimerspec *__kitp = NULL;			\
		struct __evl_timespec __kts;				\
		if (__its) {						\
			__kits.it_value = *__evl_ktimespec(		\
				&(__its)->it_value, __kts);		\
			__kits.it_interval = *__evl_ktimespec(		\
				&(__its)->it_interval, __kts);		\
			__kitp = &__kits;				\
		}							\
		__kitp;							\
	})
#endif

#define __evl_ktimespec_ptr64(__ts, __kts)	\
	__evl_ptr64(__evl_ktimespec(__ts, __kts))

#define __evl_kitimerspec_ptr64(__its, __kits)	\
	__evl_ptr64(__evl_kitimerspec(__its, __kits))

#define __evl_common_ioctl(__efd, __args...)			\
	({							\
		int __ret;					\
		if (evl_is_inband())				\
			__ret = ioctl(__efd, ##__args);		\
		else						\
			__ret = oob_ioctl(__efd, ##__args);	\
		__ret ? -errno : 0;				\
	})

/* Enable dlopen() on libevl.so. */
#define EVL_TLS_MODEL	"global-dynamic"

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
fundle_t evl_current;

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
int evl_efd;

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
struct evl_user_window *evl_current_window;

static inline int evl_get_current_mode(void)
{
	return evl_current_window ?
		evl_current_window->state : T_INBAND;
}

static inline fundle_t evl_get_current(void)
{
	return evl_current;
}

static inline struct evl_user_window *
evl_get_current_window(void)
{
	return evl_current ? evl_current_window : NULL;
}

struct evl_element_ids;

int arch_evl_init(void);

int attach_evl_clocks(void);

void init_proxy_streams(void);

int create_evl_element(const char *type,
		       const char *name,
		       void *attrs,
		       int clone_flags,
		       struct evl_element_ids *eids);

int open_evl_element_vargs(const char *type,
			const char *fmt, va_list ap);

int open_evl_element(const char *type,
		     const char *path, ...);

int create_evl_file(const char *type);

extern int (*arch_clock_gettime)(clockid_t clk_id,
				 struct timespec *tp);

extern void *evl_shared_memory;

extern int evl_ctlfd;

extern int evl_mono_clockfd;

extern int evl_real_clockfd;

#endif /* _LIB_EVL_INTERNAL_H */
