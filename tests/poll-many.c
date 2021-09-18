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
#include <evl/sem.h>
#include <evl/event.h>
#include <evl/flags.h>
#include <evl/clock.h>
#include <evl/timer.h>
#include <evl/mutex.h>
#include <evl/poll.h>
#include "helpers.h"

#define LOW_PRIO	1
#define HIGH_PRIO	2

#define NR_FEEDERS	16
#define NR_FLAGS	64

#define FEED_COUNT	  1024
#define FEEDER_PERIOD_US  3000

static struct test_flags {
	int efd;
	struct evl_flags flags;
} test_flags[NR_FLAGS];

static struct evl_event barrier;
static struct evl_mutex lock;
static bool started;

static void wait_release(void)
{
	int ret;

	__Tcall_assert(ret, evl_lock_mutex(&lock));
	for (;;) {
		if (started)
			break;
		__Tcall_assert(ret, evl_wait_event(&barrier, &lock));
	}
	__Tcall_assert(ret, evl_unlock_mutex(&lock));
}

static void *flags_feeder(void *arg)
{
	int pos = (int)(long)arg;
	int ret, tfd, n, m;

	__Tcall_assert(tfd, evl_attach_self("post-many-flags:%d.%d",
			getpid(), pos));

	wait_release();

	for (n = 0; n < FEED_COUNT; n++) {
		for (m = 0; m < NR_FLAGS; m++)
			__Tcall_assert(ret,
				evl_post_flags(&test_flags[m].flags, 1 << pos));
		evl_usleep(FEEDER_PERIOD_US);
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	struct evl_poll_event pollset[NR_FLAGS];
	int tfd, ret, n, m, pollfd, bits, nr;
	pthread_t feeders[NR_FEEDERS];
	struct timespec timeout, now;
	struct sched_param param;
	struct test_flags *p;
	void *status = NULL;
	char *name;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("poll-many:%d", getpid()));

	__Tcall_assert(ret, evl_new_event(&barrier, "poll-many-barrier:%d", getpid()));
	__Tcall_assert(ret, evl_new_mutex(&lock, "poll-many-lock:%d", getpid()));
	__Tcall_assert(pollfd, evl_new_poll());

	for (n = 0; n < NR_FLAGS; n++) {
		name = get_unique_name(EVL_MONITOR_DEV, n);
		__Tcall_assert(test_flags[n].efd,
			evl_new_flags(&test_flags[n].flags, name));
		__Tcall_assert(ret, evl_add_pollfd(pollfd, test_flags[n].efd,
					POLLIN, evl_ptrval(&test_flags[n])));
	}

	for (n = 0; n < NR_FEEDERS; n++)
		new_thread(&feeders[n], SCHED_FIFO, HIGH_PRIO,
			flags_feeder, (void *)(long)n);

	/* Release all feeders. */
	__Tcall_assert(ret, evl_lock_mutex(&lock));
	started = true;
	__Tcall_assert(ret, evl_broadcast_event(&barrier));
	__Tcall_assert(ret, evl_unlock_mutex(&lock));

	for (n = 0; n < FEED_COUNT; n++) {
		/*
		 * Cycle until we get nothing to read after a full
		 * feeder period or we reached the maximum feed count,
		 * whichever comes first.
		 */
		evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
		timespec_add_ns(&timeout, &now, FEEDER_PERIOD_US * 1000);
		nr = evl_timedpoll(pollfd, pollset, NR_FLAGS, &timeout);
		if (nr == -ETIMEDOUT)
			break;
		__Texpr_assert(nr > 0);
		for (m = 0; m < nr; m++) {
			p = pollset[m].pollval.ptr;
			__Tcall_assert(ret,
				evl_wait_flags(&p->flags, &bits));
		}
	}

	for (n = 0; n < NR_FEEDERS; n++) {
		__Texpr_assert(pthread_join(feeders[n], &status) == 0);
		__Texpr_assert(status == NULL);
	}

	return 0;
}
