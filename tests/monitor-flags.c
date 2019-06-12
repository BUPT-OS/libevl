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
#include <evl/flags.h>
#include <evl/sem.h>
#include <evl/clock.h>
#include "helpers.h"

#define LOW_PRIO	1
#define HIGH_PRIO	2

struct test_context {
	struct evl_flags flags;
	struct evl_sem start;
	struct evl_sem sem;
};

static void *flags_receiver(void *arg)
{
	struct test_context *p = arg;
	struct timespec now, timeout;
	int ret, tfd, bits;

	__Tcall_assert(tfd, evl_attach_self("monitor-flags-receiver:%d", getpid()));
	__Tcall_assert(ret, evl_get_sem(&p->start));
	evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
	timespec_add_ns(&timeout, &now, 200000000); /* 200ms */

	/* Sender is quiet: expect timeout. */
	if (!__Fcall(ret, evl_timedwait_flags(&p->flags, &timeout, &bits)) ||
		!__Texpr(ret == -ETIMEDOUT))
		goto fail;

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	/* Sender should have sent 0x12121212. */
	if (!__Tcall(ret, evl_timedwait_flags(&p->flags, &timeout, &bits)))
		goto fail;

	if (!__Texpr(bits == 0x12121212))
		goto fail;

	/* Flag group should be cleared. */
	if (!__Tcall(ret, evl_peek_flags(&p->flags, &bits)))
		goto fail;

	if (!__Texpr(bits == 0))
		goto fail;

	/* Trywait should fail with -EAGAIN. */
	if (!__Fcall(ret, evl_trywait_flags(&p->flags, &bits)))
		goto fail;

	if (!__Texpr(ret == -EAGAIN))
		goto fail;

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	/* Sender should send 0x76767676 in a moment. */
	if (!__Tcall(ret, evl_wait_flags(&p->flags, &bits)))
		goto fail;

	if (!__Texpr(bits == 0x76767676))
		goto fail;

	__Tcall_assert(ret, evl_put_sem(&p->sem));

	return NULL;
fail:
	exit(1);
}

int main(int argc, char *argv[])
{
	int tfd, ffd, sfd, ret, bits;
	struct sched_param param;
	struct test_context c;
	void *status = NULL;
	pthread_t receiver;
	char *name;

	param.sched_priority = HIGH_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("monitor-flags:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(sfd, evl_new_sem(&c.sem, EVL_CLOCK_MONOTONIC, 0, name));

	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(sfd, evl_new_sem(&c.start, EVL_CLOCK_MONOTONIC, 0, name));

	name = get_unique_name(EVL_MONITOR_DEV, 2);
	__Tcall_assert(ffd, evl_new_flags(&c.flags, EVL_CLOCK_MONOTONIC, 0, name));

	new_thread(&receiver, SCHED_FIFO, LOW_PRIO,
			flags_receiver, &c);

	__Tcall_assert(ret, evl_put_sem(&c.start));
	__Tcall_assert(ret, evl_get_sem(&c.sem));
	__Tcall_assert(ret, evl_post_flags(&c.flags, 0x12121212));
	__Tcall_assert(ret, evl_peek_flags(&c.flags, &bits));
	__Texpr_assert(bits == 0x12121212);
	__Tcall_assert(ret, evl_get_sem(&c.sem));
	__Tcall_assert(ret, evl_udelay(1000));
	__Tcall_assert(ret, evl_post_flags(&c.flags, 0x76767676));
	__Tcall_assert(ret, evl_get_sem(&c.sem));
	__Texpr_assert(pthread_join(receiver, &status) == 0);
	__Texpr_assert(status == NULL);

	evl_close_sem(&c.start);
	evl_close_sem(&c.sem);
	evl_close_flags(&c.flags);

	return 0;
}
