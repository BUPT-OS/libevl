/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_SEM_H
#define _EVENLESS_SEM_H

#include <time.h>
#include <evenless/atomic.h>
#include <linux/types.h>
#include <uapi/evenless/types.h>
#include <uapi/evenless/sem.h>

struct evl_thread;

struct evl_sem {
	unsigned int magic;
	union {
		struct {
			fundle_t fundle;
			struct evl_sem_state *state;
			int efd;
		} active;
		struct {
			const char *name;
			int clockfd;
			int flags;
			int initval;
		} uninit;
	};
};

#define __SEM_ACTIVE_MAGIC	0xcb13cb13
#define __SEM_UNINIT_MAGIC	0xed15ed15
#define __SEM_DEAD_MAGIC	0

#define EVL_SEM_INITIALIZER(__name, __clockfd, __flags, __initval)  {	\
		.magic = __SEM_UNINIT_MAGIC,				\
			.uninit = {					\
			.name = (__name),				\
			.flags = (__flags),				\
			.clockfd = (__clockfd),				\
			.initval = (__initval),				\
		}							\
	}

#ifdef __cplusplus
extern "C" {
#endif

int evl_new_sem(struct evl_sem *sem,
		int flags, int clockfd, int initval,
		const char *fmt, ...);

int evl_open_sem(struct evl_sem *sem,
		 const char *fmt, ...);
  
int evl_release_sem(struct evl_sem *sem);

int evl_get_sem(struct evl_sem *sem,
		int count,
		const struct timespec *timeout);

int evl_put_sem(struct evl_sem *sem, int count);

int evl_tryget_sem(struct evl_sem *sem, int count);

int evl_broadcast_sem(struct evl_sem *sem);

int evl_get_semval(struct evl_sem *sem);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_SEM_H */
