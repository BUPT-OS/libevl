/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include "parse_vdso.h"
#include "internal.h"

static int gettime_fallback(clockid_t clk_id, struct timespec *tp)
{
	return clock_gettime(clk_id, tp);
}

int (*arch_clock_gettime)(clockid_t clk_id, struct timespec *tp) = gettime_fallback;

int arch_evl_init(void)
{
	evl_init_vdso();
	arch_clock_gettime = evl_request_vdso("LINUX_2.6",
				 "__vdso_clock_gettime");

	return 0;
}
