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
#include <evl/mutex.h>
#include <evl/clock.h>
#include <evl/sem.h>
#include "helpers.h"

#define LOW_PRIO	1
#define MEDIUM_PRIO	2
#define HIGH_PRIO	3

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
	struct evl_mutex lock_medium, lock_high;
	struct sched_param param;
	int tfd, gfd, ret;
	char *name;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-pp-nested:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(gfd, evl_create_mutex(&lock_medium, EVL_CLOCK_MONOTONIC,
					MEDIUM_PRIO, EVL_MUTEX_NORMAL, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(gfd, evl_create_mutex(&lock_high, EVL_CLOCK_MONOTONIC,
					HIGH_PRIO, EVL_MUTEX_NORMAL, name));

	__Tcall_assert(ret, evl_lock_mutex(&lock_medium));
	/* Commit PP, expected switch to medium priority. */
	__Tcall_assert(ret, evl_usleep(1000));
	__Texpr_assert(check_priority(tfd, MEDIUM_PRIO));
	__Tcall_assert(ret, evl_lock_mutex(&lock_high));
	/* Commit PP, expected switch to high priority. */
	__Tcall_assert(ret, evl_usleep(1000));
	__Texpr_assert(check_priority(tfd, HIGH_PRIO));
	__Tcall_assert(ret, evl_unlock_mutex(&lock_high));
	__Texpr_assert(check_priority(tfd, MEDIUM_PRIO));
	__Tcall_assert(ret, evl_unlock_mutex(&lock_medium));
	__Texpr_assert(check_priority(tfd, LOW_PRIO));

	evl_close_mutex(&lock_high);
	evl_close_mutex(&lock_medium);

	return 0;
}
