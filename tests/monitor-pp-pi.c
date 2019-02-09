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
#define MEDIUM_PRIO	2
#define HIGH_PRIO	3

struct test_context {
	struct evl_monitor gate;
	struct evl_sem start;
	struct evl_sem sem;
};

static void *pi_contend_timeout(void *arg)
{
	struct test_context *p = arg;
	struct timespec now, timeout;
	int ret, tfd;

	__Tcall_assert(tfd, evl_attach_self("monitor-pi-contend:%d", getpid()));
	__Tcall_assert(ret, evl_get_sem(&p->start));

	evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
	timespec_add_ns(&timeout, &now, 10000000); /* 10ms */

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	if (__Fcall(ret, evl_enter_gate_timed(&p->gate, &timeout)) &&
		__Texpr(ret == -ETIMEDOUT))
		return (void *)1;

	return NULL;
}

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
	struct evl_monitor gate_pp;
	struct sched_param param;
	int tfd, gfd, sfd, ret;
	struct test_context c;
	pthread_t contender;
	void *status = NULL;
	char *name;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-pp-pi:%d", getpid()));

	name = get_unique_name("monitor", 0);
	__Tcall_assert(gfd, evl_new_gate(&c.gate, EVL_MONITOR_PI,
				EVL_CLOCK_MONOTONIC, 0, name));

	name = get_unique_name("monitor", 1);
	__Tcall_assert(gfd, evl_new_gate(&gate_pp, EVL_MONITOR_PP,
				EVL_CLOCK_MONOTONIC, MEDIUM_PRIO, name));

	name = get_unique_name("semaphore", 0);
	__Tcall_assert(sfd, evl_new_sem(&c.sem, 0, EVL_CLOCK_MONOTONIC,
						0, name));

	name = get_unique_name("semaphore", 1);
	__Tcall_assert(sfd, evl_new_sem(&c.start, 0, EVL_CLOCK_MONOTONIC,
						0, name));

	ret = new_thread(&contender, SCHED_FIFO, HIGH_PRIO,
			pi_contend_timeout, &c);
	if (ret < 0)
		exit(1);

	__Tcall_assert(ret, evl_enter_gate(&gate_pp));
	__Tcall_assert(ret, evl_udelay(1000)); /* Commit PP boost. */
	__Texpr_assert(check_priority(tfd, MEDIUM_PRIO));
	__Tcall_assert(ret, evl_enter_gate(&c.gate));
	__Tcall_assert(ret, evl_put_sem(&c.start));
	__Tcall_assert(ret, evl_get_sem(&c.sem));
	__Texpr_assert(check_priority(tfd, HIGH_PRIO));
	__Texpr_assert(pthread_join(contender, &status) == 0);
	__Tcall_assert(ret, evl_exit_gate(&c.gate));
	__Texpr_assert(check_priority(tfd, MEDIUM_PRIO));
	__Fexpr_assert(status == NULL);
	__Tcall_assert(ret, evl_exit_gate(&gate_pp));
	__Texpr_assert(check_priority(tfd, LOW_PRIO));

	evl_release_sem(&c.start);
	evl_release_sem(&c.sem);
	evl_release_monitor(&c.gate);
	evl_release_monitor(&gate_pp);

	return 0;
}
