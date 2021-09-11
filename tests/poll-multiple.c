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

#define NR_FEEDERS  2

#define FEED_COUNT 8192

static struct test_context {
	int efd;
	union {
		struct evl_sem sem;
		struct evl_flags flags;
	};
} c[NR_FEEDERS];

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

static void *sem_feeder(void *arg)
{
	struct test_context *p = arg;
	int ret, tfd, n;

	__Tcall_assert(tfd, evl_attach_self("poll-multi-sem:%d.%d",
			getpid(), p - c));

	wait_release();

	for (n = 0; n < FEED_COUNT; n++) {
		__Tcall_assert(ret, evl_put_sem(&p->sem));
		evl_usleep(100);
	}

	return NULL;
}

static void *flags_feeder(void *arg)
{
	struct test_context *p = arg;
	int ret, tfd, n;

	__Tcall_assert(tfd, evl_attach_self("poll-multi-flags:%d.%d",
			getpid(), p - c));

	wait_release();

	for (n = 0; n < FEED_COUNT; n++) {
		__Tcall_assert(ret, evl_post_flags(&p->flags, 1));
		evl_usleep(200);
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	struct evl_poll_event pollset[NR_FEEDERS];
	int tfd, ret, n, m, pollfd, tmfd, bits;
	struct itimerspec value, ovalue;
	pthread_t feeders[NR_FEEDERS];
	struct sched_param param;
	void *status = NULL;
	struct timespec now;
	__u64 ticks;
	char *name;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("poll-multi:%d", getpid()));

	__Tcall_assert(ret, evl_new_event(&barrier, "poll-multi-barrier:%d", getpid()));
	__Tcall_assert(ret, evl_new_mutex(&lock, "poll-multi-lock:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(c[0].efd, evl_new_sem(&c[0].sem, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(c[1].efd, evl_new_flags(&c[1].flags, name));

	__Tcall_assert(tmfd, evl_new_timer(EVL_CLOCK_MONOTONIC));

	__Tcall_assert(pollfd, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pollfd, c[0].efd, POLLIN, evl_nil));
	__Tcall_assert(ret, evl_add_pollfd(pollfd, c[1].efd, POLLIN, evl_nil));
	__Tcall_assert(ret, evl_add_pollfd(pollfd, tmfd, POLLIN, evl_nil));

	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &now));
	timespec_add_ns(&value.it_value, &now, 200000ULL);
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_nsec = 200000;
	__Tcall_assert(ret, evl_set_timer(tmfd, &value, &ovalue));

	new_thread(&feeders[0], SCHED_FIFO, HIGH_PRIO, sem_feeder, &c[0]);
	new_thread(&feeders[1], SCHED_FIFO, HIGH_PRIO, flags_feeder, &c[1]);

	/* Release all feeders. */
	__Tcall_assert(ret, evl_lock_mutex(&lock));
	started = true;
	__Tcall_assert(ret, evl_broadcast_event(&barrier));
	__Tcall_assert(ret, evl_unlock_mutex(&lock));

	for (n = 0; n < FEED_COUNT; n++) {
		__Tcall_assert(ret, evl_poll(pollfd, pollset, NR_FEEDERS));
		for (m = 0; m < ret; m++) {
			if (pollset[m].fd == tmfd) {
				__Tcall_errno_assert(ret,
					oob_read(tmfd, &ticks, sizeof(ticks)));
			} else if (pollset[m].fd == c[0].efd) {
				__Tcall_assert(ret, evl_get_sem(&c[0].sem));
			} else if (pollset[m].fd == c[1].efd) {
				__Tcall_assert(ret, evl_wait_flags(&c[1].flags, &bits));
			}
		}
	}

	value.it_interval.tv_sec = 0;
	value.it_interval.tv_nsec = 0;
	__Tcall_assert(ret, evl_set_timer(tmfd, &value, &ovalue));

	for (n = 0; n < NR_FEEDERS; n++) {
		__Texpr_assert(pthread_join(feeders[n], &status) == 0);
		__Texpr_assert(status == NULL);
	}

	return 0;
}
