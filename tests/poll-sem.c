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
#include <evl/sem.h>
#include <evl/clock.h>
#include <evl/poll.h>
#include "helpers.h"

#define NR_RECEIVERS  1

#define LOW_PRIO   1
#define HIGH_PRIO  2

#define PUT_COUNT  1024

static int pollfd_in, sfd;

static struct evl_sem sem;

struct test_context {
	int serial;
};

static void *sem_poller(void *arg)
{
	struct evl_poll_event pollset;
	struct test_context *p = arg;
	int ret, tfd, n;

	__Tcall_assert(tfd, evl_attach_self("monitor-sem-poller:%d.%d",
			getpid(), p->serial));

	for (n = 0; n < PUT_COUNT; n++) {
		__Tcall_assert(ret, evl_poll(pollfd_in, &pollset, 1));
		__Texpr_assert(ret == 1);
		__Texpr_assert(pollset.events == POLLIN);
		__Texpr_assert(pollset.fd == sfd);
		__Tcall_assert(ret, evl_tryget_sem(&sem));
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	struct test_context c[NR_RECEIVERS];
	pthread_t pollers[NR_RECEIVERS];
	struct sched_param param;
	void *status = NULL;
	int tfd, ret, n;
	char *name;

	param.sched_priority = HIGH_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("poll-sem:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(sfd, evl_new_sem(&sem, name));

	__Tcall_assert(pollfd_in, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pollfd_in, sfd, POLLIN));

	for (n = 0; n < NR_RECEIVERS; n++) {
		c[n].serial = n;
		new_thread(pollers + n, SCHED_FIFO, LOW_PRIO, sem_poller, c + n);
	}

	for (n = 0; n < PUT_COUNT; n++)
		__Tcall_assert(ret, evl_put_sem(&sem));

	for (n = 0; n < NR_RECEIVERS; n++) {
		__Texpr_assert(pthread_join(pollers[n], &status) == 0);
		__Texpr_assert(status == NULL);
	}

	__Tcall_assert(ret, evl_close_sem(&sem));

	return 0;
}
