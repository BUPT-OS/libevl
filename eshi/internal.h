/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_ESHI_INTERNAL_H
#define _EVL_ESHI_INTERNAL_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>

static inline
void timespec_add(struct timespec *r, const struct timespec *t)
{
	r->tv_sec += t->tv_sec;
	r->tv_nsec += t->tv_nsec;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}

static inline
void timespec_sub(struct timespec *r, const struct timespec *t)
{
	r->tv_sec -= t->tv_sec;
	r->tv_nsec -= t->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

static inline
void timespec_mono_to_real(struct timespec *r, const struct timespec *t)
{
	struct timespec now;

	*r = *t;
	clock_gettime(CLOCK_MONOTONIC, &now);
	timespec_sub(r, &now);
	clock_gettime(CLOCK_REALTIME, &now);
	timespec_add(r, &now);
}

pthread_t eshi_find_thread_by_fd(int fd);

int eshi_init_threads(void);

bool eshi_is_initialized(void);

#endif /* _EVL_ESHI_INTERNAL_H */
