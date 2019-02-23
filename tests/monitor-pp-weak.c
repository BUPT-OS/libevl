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

static bool check_priority(int tfd, int policy, int prio)
{
	struct evl_thread_state statebuf;
	int ret;

	__Tcall_assert(ret, evl_get_state(tfd, &statebuf));

	return statebuf.eattrs.sched_policy == policy &&
		statebuf.eattrs.sched_priority == prio;
}

int main(int argc, char *argv[])
{
	struct evl_sched_attrs attrs;
	struct evl_lock lock;
	int tfd, gfd, ret;
	char *name;

	__Tcall_assert(tfd, evl_attach_self("monitor-pp-weak:%d", getpid()));

	attrs.sched_policy = SCHED_WEAK;
	attrs.sched_priority = LOW_PRIO;
	__Tcall_assert(ret, evl_set_schedattr(tfd, &attrs));

	name = get_unique_name("monitor", 0);
	__Tcall_assert(gfd, evl_new_lock_ceiling(&lock,
				EVL_CLOCK_MONOTONIC, HIGH_PRIO, name));

	__Tcall_assert(ret, evl_lock(&lock));
	/* Commit PP, we should have inherited SCHED_FIFO, HIGH_PRIO. */
	__Tcall_assert(ret, evl_udelay(1000));
	__Texpr_assert(check_priority(tfd, SCHED_FIFO, HIGH_PRIO));
	__Tcall_assert(ret, evl_unlock(&lock));
	__Texpr_assert(check_priority(tfd, SCHED_WEAK, LOW_PRIO));

	evl_close_lock(&lock);

	return 0;
}
