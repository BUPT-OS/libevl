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
#include <evl/condvar.h>
#include <evl/sem.h>
#include <evl/syscall.h>
#include <evl/thread.h>
#include <evl/xbuf.h>
#include <evl/poll.h>
#include <evl/proxy.h>
#include <evl/utils.h>

#define __EVL__  0	/* API revision */

#ifdef __cplusplus
extern "C" {
#endif

int evl_init(void);

int evl_sigevl_handler(int sig, siginfo_t *si, void *ctxt);

void evl_sigdebug_handler(int sig, siginfo_t *si, void *ctxt);

unsigned int evl_detect_fpu(void);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_EVL_H */
