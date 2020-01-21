/*
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <stdlib.h>
#include <evl/clock.h>
#include <evl/thread.h>
#include <evl/xbuf.h>
#include <evl/poll.h>
#include "helpers.h"

static int pfd, xfd;

static void *polling_thread(void *arg)
{
	struct evl_poll_event pollset = {
		.fd = -1,
		.events = 0
	};
	int ret, tfd;

	__Tcall_assert(tfd, evl_attach_self("poller-polling:%d", getpid()));
	__Tcall_assert(ret, evl_poll(pfd, &pollset, 1));
	__Texpr_assert(pollset.fd == xfd);
	__Texpr_assert(pollset.events & POLLNVAL);

	return NULL;
}

int main(int argc, char *argv[])
{
	char *name;
	pthread_t poller;
	int tfd, ret;

	__Tcall_assert(tfd, evl_attach_self("poller-close:%d", getpid()));

	name = get_unique_name(EVL_XBUF_DEV, 0);
	__Tcall_assert(xfd, evl_new_xbuf(1024, 1024, name));

	__Tcall_assert(pfd, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd, xfd, POLLIN));

	new_thread(&poller, SCHED_FIFO, 1, polling_thread, NULL);

	/* Wait for evl_poll() to start, then close the polled fd. */
	__Tcall_assert(ret, evl_usleep(200000));
	close(xfd);
	__Texpr_assert(pthread_join(poller, NULL) == 0);
	close(pfd);

	return 0;
}
