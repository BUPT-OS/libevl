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
#include <evl/proxy.h>
#include <evl/clock.h>
#include "helpers.h"

#define LOW_PRIO	1
#define HIGH_PRIO	2

struct test_context {
	int condition;
	int wakeups;
	struct evl_mutex lock;
	struct evl_mutex other_lock;
	struct evl_event event;
	struct evl_sem start;
	struct evl_sem sync;
};

struct test_arg {
	int serial;
	struct test_context *tc;
};

static void *event_receiver(void *arg)
{
	struct test_arg *a = arg;
	struct test_context *p = a->tc;
	int ret, tfd;

	__Tcall_assert(tfd, evl_attach_self("monitor-wait-%d:%d",
				a->serial, getpid()));
	__Tcall_assert(ret, evl_put_sem(&p->sync));
	__Tcall_assert(ret, evl_get_sem(&p->start));
	__Tcall_assert(ret, evl_lock_mutex(&p->lock));

#ifndef __ESHI__
	if (a->serial > 1) {
		__Tcall_assert(ret, evl_lock_mutex(&p->other_lock));
		__Texpr_assert(evl_wait_event(&p->event, &p->other_lock) == -EBADFD);
		__Tcall_assert(ret, evl_unlock_mutex(&p->other_lock));
	}
#endif

	while (p->condition != 1)
		__Tcall_assert(ret, evl_wait_event(&p->event, &p->lock));

	p->wakeups++;

	__Tcall_assert(ret, evl_unlock_mutex(&p->lock));

	return NULL;
}

int main(int argc, char *argv[])
{
	struct test_context c = { .condition = 0, .wakeups = 0 };
	pthread_t receiver1, receiver2;
	int tfd, mfd, evfd, sfd, ret;
	struct sched_param param;
	struct test_arg a1, a2;
	void *status = NULL;
	char *name;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-wait-multiple:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(sfd, evl_new_sem(&c.start, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(sfd, evl_new_sem(&c.sync, name));

	name = get_unique_name(EVL_MONITOR_DEV, 2);
	__Tcall_assert(evfd, evl_new_event(&c.event, name));

	name = get_unique_name(EVL_MONITOR_DEV, 3);
	__Tcall_assert(mfd, evl_new_mutex(&c.lock, name));

	name = get_unique_name(EVL_MONITOR_DEV, 4);
	__Tcall_assert(mfd, evl_new_mutex(&c.other_lock, name));

	a1.tc = &c;
	a1.serial = 1;
	new_thread(&receiver1, SCHED_FIFO, HIGH_PRIO, event_receiver, &a1);
	__Tcall_assert(ret, evl_get_sem(&c.sync));
	a2.tc = &c;
	a2.serial = 2;
	new_thread(&receiver2, SCHED_FIFO, HIGH_PRIO, event_receiver, &a2);
	__Tcall_assert(ret, evl_get_sem(&c.sync));

	__Tcall_assert(ret, evl_put_sem(&c.start));
	__Tcall_assert(ret, evl_put_sem(&c.start));

	__Tcall_assert(ret, evl_lock_mutex(&c.lock));
	c.condition = 1;
	__Tcall_assert(ret, evl_broadcast_event(&c.event));
	__Tcall_assert(ret, evl_unlock_mutex(&c.lock));

	__Texpr_assert(pthread_join(receiver1, &status) == 0);
	__Texpr_assert(status == NULL);

	__Texpr_assert(pthread_join(receiver2, &status) == 0);
	__Texpr_assert(status == NULL);

	__Texpr_assert(c.wakeups == 2);
	__Tcall_assert(ret, evl_close_sem(&c.start));
	__Tcall_assert(ret, evl_close_sem(&c.sync));
	__Tcall_assert(ret, evl_close_event(&c.event));
	__Tcall_assert(ret, evl_close_mutex(&c.lock));
	__Tcall_assert(ret, evl_close_mutex(&c.other_lock));

	return 0;
}
