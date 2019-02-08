/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _LIB_EVENLESS_INTERNAL_H
#define _LIB_EVENLESS_INTERNAL_H

#include <time.h>
#include <stdarg.h>
#include <evenless/thread.h>
#include <uapi/evenless/types.h>

/*
 * Length of per-thread print formatting buffer used by
 * evl_printf_logger().
 */
#define EVL_PRINTBUF_SIZE  1024

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
fundle_t evl_current;

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
int evl_efd;

extern __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
char evl_logging_buf[EVL_PRINTBUF_SIZE];

static inline fundle_t evl_get_current(void)
{
	return evl_current;
}

static inline struct evl_user_window *
evl_get_current_window(void)
{
	return evl_current ? evl_current_window : NULL;
}

static inline int evl_should_warn(void)
{
	return (evl_get_current_mode() & (T_INBAND|T_WARN)) == T_WARN;
}

struct evl_element_ids;

int arch_evl_init(void);

int create_evl_element(const char *type,
		       const char *name,
		       void *attrs,
		       struct evl_element_ids *eids);

int open_evl_element_vargs(const char *type,
			const char *fmt, va_list ap);

int open_evl_element(const char *type,
		     const char *path, ...);

extern int (*arch_clock_gettime)(clockid_t clk_id,
				 struct timespec *tp);

extern void *evl_shared_memory;

extern int evl_ctlfd;

#endif /* _LIB_EVENLESS_INTERNAL_H */
