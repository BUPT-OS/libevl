/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVENLESS_EVL_H
#define _EVENLESS_EVL_H

#include <signal.h>

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
