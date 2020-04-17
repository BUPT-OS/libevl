/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include <evl/thread.h>
#include "helpers.h"

char *get_unique_name_and_path(const char *type,
			int serial, char **ppath)
{
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/evl/%s/test%d.%d",
		       type, getpid(), serial);
	if (ret < 0)
		error(1, ENOMEM, "malloc");

	/*
	 * Since we need a path, this has to be a public element, so
	 * we want the slash in.
	 */
	if (ppath) {
		*ppath = path;
		return strrchr(path, '/');
	}

	/* That one is private, skip the leading slash. */
	return strrchr(path, '/') + 1;
}

void new_thread(pthread_t *tid, int policy, int prio,
		void *(*fn)(void *), void *arg)
{
	struct sched_param param;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	param.sched_priority = prio;
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	pthread_attr_setschedpolicy(&attr, policy);
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	__Texpr_assert(pthread_create(tid, &attr, fn, arg) == 0);
}

void timespec_add_ns(struct timespec *__restrict r,
		const struct timespec *__restrict t,
		long ns)
{
	long s, rem;

	s = ns / 1000000000;
	rem = ns - s * 1000000000;
	r->tv_sec = t->tv_sec + s;
	r->tv_nsec = t->tv_nsec + rem;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}
