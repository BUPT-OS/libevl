/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>
#include <evl/proxy.h>
#include <evl/poll.h>
#include <evl/sem.h>
#include <evl/syscall.h>
#include "helpers.h"

#define TEST_VALUE1 0x55aa2711
#define TEST_VALUE2 0xff774421

static int evntfd, proxyfd;

static struct evl_sem ready;

static void *poller(void *arg)
{
	struct evl_poll_event pollset;
	int ret, tfd, pfd;
	uint64_t val;

	__Tcall_assert(tfd, evl_attach_self("proxy-poller:%d", getpid()));

	__Tcall_assert(pfd, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd, proxyfd, POLLIN, evl_intval(TEST_VALUE2)));

	__Tcall_assert(ret, evl_poll(pfd, &pollset, 1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(pollset.events == POLLIN);
	__Texpr_assert(pollset.pollval.val == TEST_VALUE2);

	__Tcall_errno_assert(ret, oob_read(proxyfd, &val, sizeof(val)));
	__Texpr_assert(ret == sizeof(val));
	__Texpr_assert(val == TEST_VALUE1);

	__Tcall_assert(ret, evl_put_sem(&ready));

	val = TEST_VALUE2;
	__Tcall_errno_assert(ret, oob_write(proxyfd, &val, sizeof(val)));
	__Texpr_assert(ret == sizeof(val));

	return NULL;
}

int main(int argc, char *argv[])
{
	struct epoll_event ev;
	int tfd, pollfd, ret;
	pthread_t tid;
	uint64_t val;

	__Tcall_assert(evntfd, eventfd(TEST_VALUE1, 0));
	__Tcall_assert(proxyfd, evl_create_proxy(evntfd, sizeof(uint64_t),
				sizeof(uint64_t), EVL_CLONE_INPUT|EVL_CLONE_OUTPUT,
				"proxy-poll:%d", getpid()));

	__Tcall_assert(tfd, evl_attach_self("proxy-poll:%d", getpid()));
	__Tcall_assert(ret, evl_new_sem(&ready, "proxy-poll-ready:%d", getpid()));
	new_thread(&tid, SCHED_FIFO, 1, poller, NULL);
	__Tcall_assert(ret, evl_get_sem(&ready));

	__Tcall_assert(pollfd, epoll_create1(0));
	ev.events = EPOLLIN;
	ev.data.fd = proxyfd;
	__Tcall_errno_assert(ret, epoll_ctl(pollfd, EPOLL_CTL_ADD, proxyfd, &ev));

	memset(&ev, 0, sizeof(ev));
	__Tcall_assert(ret, epoll_wait(pollfd, &ev, 1, -1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(ev.events == EPOLLIN);
	__Texpr_assert(ev.data.fd == proxyfd);

	__Tcall_errno_assert(ret, read(evntfd, &val, sizeof(val)));
	__Texpr_assert(ret == sizeof(val));
	__Texpr_assert(val == TEST_VALUE2);

	pthread_join(tid, NULL);

	return 0;
}
