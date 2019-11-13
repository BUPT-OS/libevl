/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <evl/evl.h>
#include "internal.h"

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static int init_status = -ENXIO;

static void atfork_handler(void)
{
	init_once = PTHREAD_ONCE_INIT;
	init_status = 0;
}

static inline int do_init(void)
{
	pthread_atfork(NULL, NULL, atfork_handler);

	return eshi_init_threads();
}

static void do_init_once(void)
{
	init_status = do_init();
}

int evl_init(void)
{
	pthread_once(&init_once, do_init_once);

	return init_status;
}

bool eshi_is_initialized(void)
{
	return init_status == 0;
}
