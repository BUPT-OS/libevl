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
#include <evl/event.h>
#include <evl/mutex.h>
#include <evl/sem.h>
#include <evl/clock.h>
#include "helpers.h"

#define LOW_PRIO	1
#define HIGH_PRIO	2

struct test_context {
	int condition;
	struct evl_mutex lock;
	struct evl_event event;
	struct evl_sem start;
	struct evl_sem sem;
};

static int receiverfd;

static void *event_receiver(void *arg)
{
	struct test_context *p = arg;
	struct timespec now, timeout;
	int ret;

	__Tcall_assert(receiverfd, evl_attach_self("monitor-event-receiver:%d", getpid()));
	__Tcall_assert(ret, evl_get_sem(&p->start));
	evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
	timespec_add_ns(&timeout, &now, 200000000); /* 200ms */

	__Tcall_assert(ret, evl_lock_mutex(&p->lock));

	if (p->condition)
		goto fail;	/* uhh? */

	/* Condition stays false: expect timeout. */
	if (!__Fcall(ret, evl_timedwait_event(&p->event, &p->lock, &timeout)) ||
		!__Texpr(ret == -ETIMEDOUT))
		goto fail;

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	/* Simple signal. */

	while (p->condition != 1)
		__Tcall_assert(ret, evl_wait_event(&p->event, &p->lock));

	__Tcall_assert(ret, evl_unlock_mutex(&p->lock));

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	/* Broadcast signal. */

	__Tcall_assert(ret, evl_lock_mutex(&p->lock));

	while (p->condition != 2)
		__Tcall_assert(ret, evl_wait_event(&p->event, &p->lock));

	__Tcall_assert(ret, evl_unlock_mutex(&p->lock));

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	/* Targeted signal. */

	__Tcall_assert(ret, evl_lock_mutex(&p->lock));

	while (p->condition != 3)
		__Tcall_assert(ret, evl_wait_event(&p->event, &p->lock));

	__Tcall_assert(ret, evl_unlock_mutex(&p->lock));

	return NULL;
fail:
	exit(1);
}

int main(int argc, char *argv[])
{
	struct test_context c = { .condition = 0 };
	int tfd, mfd, evfd, sfd, ret;
	struct sched_param param;
	void *status = NULL;
	pthread_t receiver;
	char *name;

	param.sched_priority = HIGH_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-event:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(sfd, evl_new_sem(&c.sem, EVL_CLOCK_MONOTONIC, 0, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(sfd, evl_new_sem(&c.start, EVL_CLOCK_MONOTONIC, 0, name));

	name = get_unique_name(EVL_MONITOR_DEV, 2);
	__Tcall_assert(evfd, evl_new_event(&c.event, EVL_CLOCK_MONOTONIC, name));

	name = get_unique_name(EVL_MONITOR_DEV, 3);
	__Tcall_assert(mfd, evl_new_mutex(&c.lock, EVL_MUTEX_NORMAL,
						EVL_CLOCK_MONOTONIC, name));

	new_thread(&receiver, SCHED_FIFO, LOW_PRIO, event_receiver, &c);

	__Tcall_assert(ret, evl_put_sem(&c.start));

	__Tcall_assert(ret, evl_get_sem(&c.sem));
	__Tcall_assert(ret, evl_lock_mutex(&c.lock));
	c.condition = 1;
	__Tcall_assert(ret, evl_signal_event(&c.event));
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock));

	__Tcall_assert(ret, evl_get_sem(&c.sem));
	__Tcall_assert(ret, evl_lock_mutex(&c.lock));
	c.condition = 2;
	__Tcall_assert(ret, evl_broadcast_event(&c.event));
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock));

	__Tcall_assert(ret, evl_get_sem(&c.sem));

	/* Wait for the receiver to enter evl_wait_event() */
	__Tcall_assert(ret, evl_udelay(1000));

	__Tcall_assert(ret, evl_lock_mutex(&c.lock));
	c.condition = 3;
#ifdef __ESHI__
	__Tcall_assert(ret, evl_signal_event(&c.event));
#else
	__Tcall_assert(ret, evl_signal_thread(&c.event, receiverfd));
#endif
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock));

	__Texpr_assert(pthread_join(receiver, &status) == 0);
	__Texpr_assert(status == NULL);

	__Tcall_assert(ret, evl_close_sem(&c.start));
	__Tcall_assert(ret, evl_close_sem(&c.sem));
	__Tcall_assert(ret, evl_close_event(&c.event));
	__Tcall_assert(ret, evl_close_mutex(&c.lock));

	return 0;
}
