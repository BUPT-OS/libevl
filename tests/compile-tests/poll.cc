/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/poll.h>

int main(int argc, char *argv[])
{
	struct evl_poll_event pollset;
	struct timespec timeout;
	int efd;

	efd = evl_new_poll();
	evl_add_pollfd(efd, 1, POLLIN, evl_nil);
	evl_del_pollfd(efd, 1);
	evl_mod_pollfd(efd, 1, POLLOUT, evl_nil);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_poll(efd, &pollset, 1);
	evl_timedpoll(efd, &pollset, 1, &timeout);

	return 0;
}
