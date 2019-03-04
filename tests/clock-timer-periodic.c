/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evenless/thread.h>
#include <evenless/timer.h>
#include <evenless/clock.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	struct itimerspec value, ovalue;
	struct timespec now;
	int tmfd, ret, n;
	__u32 ticks;

	ret = evl_attach_self("periodic-timer:%d", getpid());
	/*
	 * evl_init() was indirectly called when attaching, so we may
	 * create a timer from that point.
	 */
	__Tcall_assert(tmfd, evl_new_timer(EVL_CLOCK_MONOTONIC));
	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &now));
	timespec_add_ns(&value.it_value, &now, 1000000000ULL);
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_nsec = 5000000;
	__Tcall_assert(ret, evl_set_timer(tmfd, &value, &ovalue));

	for (n = 0; n < 200; n++) {
		__Tcall_assert(ret, oob_read(tmfd, &ticks, sizeof(ticks)));
		__Texpr_assert(ticks == 1);
	}

	value.it_interval.tv_sec = 0;
	value.it_interval.tv_nsec = 0;
	__Tcall_assert(ret, evl_set_timer(tmfd, &value, &ovalue));

	return 0;
}
