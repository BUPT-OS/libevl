/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_EVL_H
#define _EVENLESS_EVL_H

#include <signal.h>
#include <evenless/clock.h>
#include <evenless/logger.h>
#include <evenless/mapper.h>
#include <evenless/monitor.h>
#include <evenless/sem.h>
#include <evenless/syscall.h>
#include <evenless/thread.h>
#include <evenless/xbuf.h>
#include <evenless/poller.h>

#define __EVL__  0	/* API revision */

#ifdef __cplusplus
extern "C" {
#endif

int evl_init(void);

int evl_sigshadow_handler(int sig, siginfo_t *si, void *ctxt);

void evl_sigdebug_handler(int sig, siginfo_t *si, void *ctxt);

unsigned int evl_detect_fpu(void);

#ifdef __cplusplus
}
#endif

#endif /* _EVENLESS_EVL_H */