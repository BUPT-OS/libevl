/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <evenless/thread.h>
#include <evenless/monitor.h>
#include <evenless/clock.h>
#include <evenless/sem.h>
#include "helpers.h"

#define LOW_PRIO	1
#define HIGH_PRIO	2

static bool check_priority(int tfd, int prio)
{
	struct evl_thread_state statebuf;
	int ret;

	__Tcall_assert(ret, evl_get_state(tfd, &statebuf));

	return statebuf.eattrs.sched_policy == SCHED_FIFO &&
		statebuf.eattrs.sched_priority == prio;
}

int main(int argc, char *argv[])
{
	struct sched_param param;
	struct evl_lock lock;
	int tfd, gfd, ret;
	char *name;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-pp-dynamic:%d", getpid()));

	name = get_unique_name("monitor", 0);
	__Tcall_assert(gfd, evl_new_lock_ceiling(&lock, EVL_CLOCK_MONOTONIC,
					LOW_PRIO, name));
	__Tcall_assert(ret, evl_lock(&lock));
	/* Commit PP boost, no priority change expected. */
	__Tcall_assert(ret, evl_udelay(1000));
	__Texpr_assert(check_priority(tfd, LOW_PRIO));
	__Tcall_assert(ret, evl_unlock(&lock));
	__Tcall_assert(ret, evl_set_lock_ceiling(&lock, HIGH_PRIO));
	__Tcall_assert(ret, evl_lock(&lock));
	/* Commit PP boost, should be boosted to HIGH_PRIO. */
	__Tcall_assert(ret, evl_udelay(1000));
	__Texpr_assert(check_priority(tfd, HIGH_PRIO));
	__Tcall_assert(ret, evl_unlock(&lock));

	evl_close_lock(&lock);

	return 0;
}
