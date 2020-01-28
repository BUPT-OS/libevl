/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <evl/thread.h>
#include <evl/timer.h>
#include <evl/clock.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	struct itimerspec value, ovalue;
	struct sched_param param;
	struct timespec now;
	int tmfd, ret, n;
	__u64 ticks;

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	/* EVL inherits the inband scheduling params upon attachment. */
	ret = evl_attach_self("periodic-timer:%d", getpid());
	/*
	 * evl_init() was indirectly called when attaching, so we may
	 * create a timer from that point.
	 */
	__Tcall_assert(tmfd, evl_new_timer(EVL_CLOCK_MONOTONIC));
	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &now));
	timespec_add_ns(&value.it_value, &now, 1000000000ULL);
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_nsec = 10000000;
	__Tcall_assert(ret, evl_set_timer(tmfd, &value, &ovalue));

	for (n = 0; n < 200; n++) {
		__Tcall_errno_assert(ret, oob_read(tmfd, &ticks, sizeof(ticks)));
		__Texpr_assert(ticks == 1);
	}

	value.it_interval.tv_sec = 0;
	value.it_interval.tv_nsec = 0;
	__Tcall_assert(ret, evl_set_timer(tmfd, &value, &ovalue));

	return 0;
}
