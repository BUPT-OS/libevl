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
#include <evl/clock.h>
#include <evl/sem.h>
#include "helpers.h"

struct test_context {
	struct evl_sem start;
	struct evl_sem sem;
};

static void *sem_contend(void *arg)
{
	struct test_context *p = arg;
	int ret, tfd;

	__Tcall_assert(tfd, evl_attach_self("sem-unblock-contend:%d", getpid()));
	__Tcall_assert(ret, evl_put(&p->start));
	__Fcall_assert(ret, evl_get(&p->sem));
	__Texpr_assert(ret == -EIDRM);
	__Fcall_assert(ret, evl_get(&p->sem));
	__Texpr_assert(ret == -EBADF);

	return NULL;
}

int main(int argc, char *argv[])
{
	struct test_context c;
	pthread_t contender;
	int tfd, sfd, ret;
	char *name;

	__Tcall_assert(tfd, evl_attach_self("sem-close-unblock:%d", getpid()));

	name = get_unique_name("monitor", 0);
	__Tcall_assert(sfd, evl_new_sem(&c.sem, EVL_CLOCK_MONOTONIC, 0, name));
	name = get_unique_name("monitor", 1);
	__Tcall_assert(sfd, evl_new_sem(&c.start, EVL_CLOCK_MONOTONIC, 0, name));
	ret = new_thread(&contender, SCHED_FIFO, 1, sem_contend, &c);
	if (ret < 0)
		exit(1);

	__Tcall_assert(ret, evl_get(&c.start));
	__Tcall_assert(ret, evl_close_sem(&c.sem));
	pthread_join(contender, NULL);

	evl_close_sem(&c.start);

	return 0;
}
