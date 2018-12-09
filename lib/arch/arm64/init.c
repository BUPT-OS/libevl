/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include "parse_vdso.h"
#include "internal.h"

int (*arch_clock_gettime)(clockid_t clk_id, struct timespec *tp);

int arch_evl_init(void)
{
	evl_init_vdso();
	arch_clock_gettime = evl_request_vdso("LINUX_2.6.39",
				 "__kernel_clock_gettime");

	return 0;
}
