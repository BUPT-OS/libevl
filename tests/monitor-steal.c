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
#define HIGH_PRIO	2

struct test_context {
	struct evl_mutex lock;
	struct evl_sem start;
	bool acquired;
};

static void *victim(void *arg)
{
	struct test_context *p = arg;
	int ret, tfd;

	if (!__Tcall(tfd, evl_attach_self("monitor-victim:%d", getpid())))
		return (void *)(long)tfd;

	/*
	 * Drop the only fileref on this thread so that its resources
	 * are released right after it exits.
	 */
	close(tfd);

	if (!__Tcall(ret, evl_get_sem(&p->start)))
		return (void *)(long)ret;

	if (!__Tcall(ret, evl_lock_mutex(&p->lock)))
		return (void *)(long)ret;

	p->acquired = true;

	if (!__Tcall(ret, evl_unlock_mutex(&p->lock)))
		return (void *)(long)ret;

	return NULL;
}

static void test_steal(bool do_steal)
{
	struct test_context c;
	pthread_t contender;
	void *status = NULL;
	int gfd, sfd, ret;
	char *name;

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(gfd, evl_new_mutex(&c.lock, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(sfd, evl_new_sem(&c.start, name));
	c.acquired = false;

	new_thread(&contender, SCHED_FIFO, LOW_PRIO, victim, &c);

	__Tcall_assert(ret, evl_lock_mutex(&c.lock));

	__Tcall_assert(ret, evl_put_sem(&c.start));

	/*
	 * Wait for the victim to block on the lock. Sleep for 200ms
	 * (for slow VMs)
	 */
	evl_usleep(200000);

	/*
	 * Pass the lock ownership to the victim, but we have higher
	 * priority so it can't resume just yet. If @do_steal is true,
	 * we should be able to steal the lock from it.
	 */
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock));

	if (do_steal) {
		__Tcall_assert(ret, evl_lock_mutex(&c.lock));	/* Steal it. */
		__Fexpr_assert(c.acquired);
	} else {
		evl_usleep(200000);
		__Tcall_assert(ret, evl_lock_mutex(&c.lock));
		__Texpr_assert(c.acquired);
	}

	__Tcall_assert(ret, evl_unlock_mutex(&c.lock));
	__Texpr_assert(pthread_join(contender, &status) == 0);
	__Texpr_assert(status == NULL);

	evl_close_sem(&c.start);
	evl_close_mutex(&c.lock);
}

int main(int argc, char *argv[])
{
	struct sched_param param;
	int tfd;

	param.sched_priority = HIGH_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	__Tcall_assert(tfd, evl_attach_self("monitor-steal:%d", getpid()));

	test_steal(true);
	test_steal(false);

	return 0;
}
