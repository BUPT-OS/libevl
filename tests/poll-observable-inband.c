/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <evl/thread.h>
#include <evl/observable.h>
#include <evl/clock.h>
#include <evl/poll.h>
#include <evl/sem.h>
#include "helpers.h"

#define NR_RECEIVERS  1

#define LOW_PRIO   1

#define WRITE_COUNT  1024

static int pollfd_in, ofd;

struct test_context {
	int serial;
};

static struct evl_sem ready;

static void *observable_poller(void *arg)
{
	struct test_context *p = arg;
	struct evl_notification nf;
	struct epoll_event evs;
	int ret, tfd, n;

	__Tcall_assert(tfd, evl_attach_self("observable-poller:%d.%d",
			getpid(), p->serial));

	__Tcall_assert(ret, evl_subscribe(ofd, WRITE_COUNT, 0));

	evl_put_sem(&ready);

	for (n = 0; n < WRITE_COUNT; n++) {
		__Tcall_assert(ret, epoll_wait(pollfd_in, &evs, 1, -1));
		__Texpr_assert(ret == 1);
		__Texpr_assert(evs.events == POLLIN);
		__Texpr_assert(evs.data.fd == ofd);
		__Tcall_assert(ret, evl_read_observable(ofd, &nf, 1));
		__Texpr_assert(ret == 1);
		__Texpr_assert(nf.tag == EVL_NOTICE_USER);
		__Texpr_assert(nf.event.val == 0xa5a5a5a5);
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	struct test_context c[NR_RECEIVERS];
	pthread_t pollers[NR_RECEIVERS];
	struct sched_param param;
	struct evl_notice next;
	struct epoll_event ev;
	void *status = NULL;
	int tfd, ret, n;

	param.sched_priority = LOW_PRIO;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	/* EVL inherits the inband scheduling params upon attachment. */
	__Tcall_assert(tfd, evl_attach_self("poll-observable-inband:%d", getpid()));

	__Tcall_assert(ofd, evl_new_observable("observable:%d", getpid()));
	__Tcall_assert(ret, evl_new_sem(&ready, "poll-observable-inband-ready:%d", getpid()));

	__Tcall_assert(pollfd_in, epoll_create1(0));
	ev.events = POLLIN;
	ev.data.fd = ofd;
	__Tcall_errno_assert(ret, epoll_ctl(pollfd_in, EPOLL_CTL_ADD, ofd, &ev));

	for (n = 0; n < NR_RECEIVERS; n++) {
		c[n].serial = n;
		new_thread(pollers + n, SCHED_OTHER, 0, observable_poller, c + n);
		__Tcall_assert(ret, evl_get_sem(&ready));
	}

	for (n = 0; n < WRITE_COUNT; n++) {
		next.tag = EVL_NOTICE_USER;
		next.event.val = 0xa5a5a5a5;
		__Tcall_assert(ret, evl_update_observable(ofd, &next, 1));
		__Texpr_assert(ret == 1);
	}

	for (n = 0; n < NR_RECEIVERS; n++) {
		__Texpr_assert(pthread_join(pollers[n], &status) == 0);
		__Texpr_assert(status == NULL);
	}

	return 0;
}
