/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/timer.h>

int main(int argc, char *argv[])
{
	struct itimerspec its, ots;
	int efd;

	efd = evl_new_timer(EVL_CLOCK_MONOTONIC);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &its.it_value);
	its.it_interval.tv_sec = 1;
	its.it_interval.tv_nsec = 0;
	evl_set_timer(efd, &its, &ots);
	evl_get_timer(efd, &ots);

	return 0;
}
