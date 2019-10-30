/*
 * SPDX-License-Identifier: MIT
 *
 * Detect ABBA deadlock pattern without involving the PI chain.
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
#define HIGH_PRIO	2

struct test_context {
	struct evl_mutex lock_a;
	struct evl_mutex lock_b;
	struct evl_sem start;
	struct evl_sem sync;
};

static void *deadlocking_thread(void *arg)
{
	struct test_context *p = arg;
	int ret, tfd, mode = T_WOLI;

	__Tcall_assert(tfd, evl_attach_self("monitor-dlk-B:%d", getpid()));
	/*
	 * Disable WOLI in case CONFIG_EVL_DEBUG_WOLI is set, as we
	 * are about to sleep while holding a mutex.
	 */
	__Tcall_errno_assert(ret, oob_ioctl(tfd, EVL_THRIOC_CLEAR_MODE, &mode));
	__Tcall_assert(ret, evl_lock_mutex(&p->lock_b));
	__Tcall_assert(ret, evl_put_sem(&p->sync));
	__Tcall_assert(ret, evl_get_sem(&p->start));
	__Fcall_assert(ret, evl_lock_mutex(&p->lock_a)); /* ABBA deadlock */
	__Texpr_assert(ret == -EDEADLK);
	__Tcall_assert(ret, evl_unlock_mutex(&p->lock_b));

	return NULL;
}

int main(int argc, char *argv[])
{
	int tfd, gfd, sfd, ret, mode = T_WOLI;
	struct sched_param param;
	struct test_context c;
	pthread_t deadlocker;
	char *name;

	param.sched_priority = HIGH_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-dlk-A:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(gfd, evl_new_mutex(&c.lock_a, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(gfd, evl_new_mutex(&c.lock_b, name));

	name = get_unique_name(EVL_MONITOR_DEV, 2);
	__Tcall_assert(sfd, evl_new_sem(&c.sync, name));

	name = get_unique_name(EVL_MONITOR_DEV, 3);
	__Tcall_assert(sfd, evl_new_sem(&c.start, name));

	new_thread(&deadlocker, SCHED_FIFO, LOW_PRIO, deadlocking_thread, &c);

	/*
	 * Disable WOLI in case CONFIG_EVL_DEBUG_WOLI is set, as we
	 * are about to sleep while holding a mutex.
	 */
	__Tcall_errno_assert(ret, oob_ioctl(tfd, EVL_THRIOC_CLEAR_MODE, &mode));
	__Tcall_assert(ret, evl_lock_mutex(&c.lock_a));
	__Fcall_assert(ret, evl_lock_mutex(&c.lock_a)); /* stupid deadlock */
	__Texpr_assert(ret == -EDEADLK);
	__Tcall_assert(ret, evl_get_sem(&c.sync));
	__Tcall_assert(ret, evl_put_sem(&c.start));
	__Tcall_assert(ret, evl_lock_mutex(&c.lock_b));
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock_a));
	__Texpr_assert(pthread_join(deadlocker, NULL) == 0);

	evl_close_sem(&c.start);
	evl_close_sem(&c.sync);
	evl_close_mutex(&c.lock_a);
	evl_close_mutex(&c.lock_b);

	return 0;
}
