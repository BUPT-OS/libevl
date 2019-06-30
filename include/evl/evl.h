/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_EVL_H
#define _EVL_EVL_H

#include <signal.h>
#include <evl/clock.h>
#include <evl/mutex.h>
#include <evl/event.h>
#include <evl/sem.h>
#include <evl/flags.h>
#include <evl/syscall.h>
#include <evl/thread.h>
#include <evl/sched.h>
#include <evl/xbuf.h>
#include <evl/poll.h>
#include <evl/proxy.h>

#define __EVL__  3	/* API revision */

#ifdef __cplusplus
extern "C" {
#endif

int evl_init(void);

int evl_sigevl_handler(int sig, siginfo_t *si, void *ctxt);

void evl_sigdebug_handler(int sig, siginfo_t *si, void *ctxt);

unsigned int evl_detect_fpu(void);

extern const char *libevl_version_string;

#ifdef __cplusplus
}
#endif

#endif /* _EVL_EVL_H */
