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
#include "helpers.h"

char *get_unique_name_and_path(const char *type,
			int serial, char **ppath)
{
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/evenless/%s/test%d.%d",
		       type, getpid(), serial);
	if (ret < 0)
		error(1, ENOMEM, "malloc");

	if (ppath)
		*ppath = path;

	return strrchr(path, '/') + 1;
}

int new_thread(pthread_t *tid, int policy, int prio,
		void *(*fn)(void *), void *arg)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	param.sched_priority = prio;
	pthread_attr_setschedpolicy(&attr, policy);
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	__Tcall(ret, pthread_create(tid, &attr, fn, arg));

	return ret;
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
