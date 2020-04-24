/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <evl/thread.h>
#include <evl/flags.h>
#include <evl/clock.h>
#include <evl/poll.h>
#include "helpers.h"

#define NR_RECEIVERS  1

#define LOW_PRIO   1
#define HIGH_PRIO  2

static int pollfd_in, pollfd_out, ffd;

static struct evl_flags flags;

struct test_context {
	int serial;
};

static void *flags_poller(void *arg)
{
	struct evl_poll_event pollset;
	struct test_context *p = arg;
	struct timespec now, timeout;
	int ret, tfd, bits;

	__Tcall_assert(tfd, evl_attach_self("monitor-flags-poller:%d.%d",
			getpid(), p->serial));

	do {
		__Tcall_assert(ret, evl_poll(pollfd_in, &pollset, 1));
		__Texpr_assert(ret == 1);
		__Texpr_assert(pollset.events == POLLIN);
		__Texpr_assert(pollset.fd == ffd);
		evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
		timespec_add_ns(&timeout, &now, 1000000000);
		__Tcall_assert(ret, evl_timedwait_flags(&flags, &timeout, &bits));
	} while (bits != 0x80000000);

	return NULL;
}

int main(int argc, char *argv[])
{
	struct test_context c[NR_RECEIVERS];
	pthread_t pollers[NR_RECEIVERS];
	struct evl_poll_event pollset;
	struct sched_param param;
	void *status = NULL;
	int tfd, ret, n;
	char *name;

	param.sched_priority = HIGH_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("poll-flags:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(ffd, evl_new_flags(&flags, name));

	__Tcall_assert(pollfd_in, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pollfd_in, ffd, POLLIN, evl_nil));
	__Tcall_assert(pollfd_out, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pollfd_out, ffd, POLLOUT, evl_nil));

	for (n = 0; n < NR_RECEIVERS; n++) {
		c[n].serial = n;
		new_thread(pollers + n, SCHED_FIFO, LOW_PRIO, flags_poller, c + n);
	}

	for (n = 1; n != 0; n <<= 1) {
		/* Wait for the flag group to be clear. */
		__Tcall_assert(ret, evl_poll(pollfd_out, &pollset, 1));
		__Texpr_assert(ret == 1);
		__Texpr_assert(pollset.events == POLLOUT);
		__Texpr_assert(pollset.fd == ffd);
		/* Then post the next pattern. */
		__Tcall_assert(ret, evl_post_flags(&flags, n));
	}

	for (n = 0; n < NR_RECEIVERS; n++) {
		__Texpr_assert(pthread_join(pollers[n], &status) == 0);
		__Texpr_assert(status == NULL);
	}

	__Tcall_assert(ret, evl_close_flags(&flags));

	return 0;
}
