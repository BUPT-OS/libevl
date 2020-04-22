/*
 * SPDX-License-Identifier: MIT
 *
 * Detect ABBA deadlock pattern walking the PI chain.
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
#define MID_PRIO	2
#define HIGH_PRIO	3

struct test_context {
	struct evl_mutex lock_a;
	struct evl_mutex lock_b;
	struct evl_sem start;
	struct evl_sem sync;
};

static void *deadlocking_thread(void *arg)
{
	struct test_context *p = arg;
	int ret, tfd;

	__Tcall_assert(tfd, evl_attach_self("monitor-pi-dlk-B:%d", getpid()));
	/*
	 * Disable WOLI in case CONFIG_EVL_DEBUG_WOLI is set, as we
	 * are about to sleep while holding a mutex.
	 */
	__Tcall_assert(ret, evl_clear_thread_mode(tfd, T_WOLI, NULL));
	__Tcall_assert(ret, evl_lock_mutex(&p->lock_b));
	__Tcall_assert(ret, evl_put_sem(&p->sync));
	__Tcall_assert(ret, evl_get_sem(&p->start));
	__Fcall_assert(ret, evl_lock_mutex(&p->lock_a)); /* ABBA deadlock */
	__Texpr_assert(ret == -EDEADLK);
	__Tcall_assert(ret, evl_unlock_mutex(&p->lock_b));

	return NULL;
}

static void *starter_thread(void *arg)
{
	struct test_context *p = arg;
	int tfd, ret;

	__Tcall_assert(tfd, evl_attach_self("monitor-pi-dlk-S:%d", getpid()));
	__Tcall_assert(ret, evl_get_sem(&p->sync));
	__Tcall_assert(ret, evl_put_sem(&p->start));

	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t deadlocker, starter;
	struct sched_param param;
	int tfd, gfd, sfd, ret;
	struct test_context c;
	char *name;

	param.sched_priority = MID_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-pi-dlk-A:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(gfd, evl_new_mutex(&c.lock_a, name)); /* PI-implicit form. */

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(gfd, evl_new_mutex(&c.lock_b, name));

	name = get_unique_name(EVL_MONITOR_DEV, 2);
	__Tcall_assert(sfd, evl_new_sem(&c.sync, name));

	name = get_unique_name(EVL_MONITOR_DEV, 3);
	__Tcall_assert(sfd, evl_new_sem(&c.start, name));

	new_thread(&starter, SCHED_FIFO, LOW_PRIO, starter_thread, &c);
	new_thread(&deadlocker, SCHED_FIFO, HIGH_PRIO, deadlocking_thread, &c);

	__Tcall_assert(ret, evl_get_sem(&c.sync));
	__Tcall_assert(ret, evl_lock_mutex(&c.lock_a));
	__Tcall_assert(ret, evl_put_sem(&c.sync));
	__Tcall_assert(ret, evl_lock_mutex(&c.lock_b));
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock_a));
	__Texpr_assert(pthread_join(deadlocker, NULL) == 0);
	__Texpr_assert(pthread_join(starter, NULL) == 0);

	evl_close_sem(&c.start);
	evl_close_sem(&c.sync);
	evl_close_mutex(&c.lock_a);
	evl_close_mutex(&c.lock_b);

	return 0;
}
