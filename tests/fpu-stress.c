/*
 * SPDX-License-Identifier: MIT
 *
 * Adapted from Xenomai's smokey/fpu-test test (https://xenomai.org/)
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * PURPOSE: stresses the out-of-band context switching code with
 * respect to FPU management, checking that the unit is safely shared
 * between the in-band and out-of-band execution contexts. This test
 * borrows the testing logic from the 'hectic' benchmarking program.
 */

#include <sched.h>
#include <pthread.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <evl/evl.h>
#include <uapi/asm/evl/fptest.h>
#include "helpers.h"

#define TEST_PERIOD_USEC 1000UL

#define do_trace(__fmt, __args...)				\
	do {							\
		if (verbose)					\
			evl_printf(__fmt "\n", ##__args);	\
	} while (0)

static unsigned int fp_features;

static bool verbose;

static int timeout_secs = 3;	/* Default to 3s runtime. */

static void *stress_loop(void *arg)
{
	int tfd;

	__Tcall_assert(tfd, evl_attach_self("fpu-stresser:%d", getpid()));

	for (;;) {
		evl_set_fpregs(fp_features, 0xf1f5f1f5);
		evl_usleep(TEST_PERIOD_USEC);
	}

	return NULL;
}

static void usage(void)
{
        fprintf(stderr, "usage: fpu-stress [options]:\n");
        fprintf(stderr, "-T --timeout=<seconds>       seconds before test stops (0 means never/infinite) [=3]\n");
        fprintf(stderr, "-v --verbose                 turn on verbosity\n");
}

#define short_optlist "T:v"

static const struct option options[] = {
	{
		.name = "timeout",
		.has_arg = required_argument,
		.val = 'T',
	},
	{
		.name = "verbose",
		.has_arg = no_argument,
		.val = 'v',
	},
	{ /* Sentinel */ }
};

static void fail(unsigned int fpval, int bad_reg)
{
	if (bad_reg < 0)
		do_trace("FPU corruption detected");
	else
		do_trace("fpreg%d corrupted", bad_reg);

	__Texpr_assert(0);
}

int main(int argc, char *argv[])
{
	unsigned int sleep_ms, n, loops;
	struct timespec ts;
	int c, bad_reg;
	pthread_t tid;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 'v':
			verbose = true;
			break;
		case 'T':
			timeout_secs = atoi(optarg);
			if (timeout_secs < 0)
				error(1, EINVAL, "invalid timeout");
			break;
		case '?':
		default:
			usage();
			return 1;
		}
	}

	if (optind < argc) {
		usage();
		return 1;
	}

	/*
	 * Do not attach to the core. We want the main thread to keep
	 * running as a plain POSIX thread.
	 */

	fp_features = evl_detect_fpu();
	if (fp_features == 0)
		return EXIT_NO_SUPPORT;

	ts.tv_sec = 0;
	ts.tv_nsec = TEST_PERIOD_USEC * 1000;
	sleep_ms = 1000000UL / ts.tv_nsec;
	loops = timeout_secs * 1000UL / sleep_ms;

	new_thread(&tid, SCHED_FIFO, 10, stress_loop, NULL);

	if (loops)
		do_trace("running for %d seconds", timeout_secs);
	else
		do_trace("running indefinitely...");

	for (n = 0; loops == 0 || n < loops; n++) {
		evl_set_fpregs(fp_features, n);
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
		if (evl_check_fpregs(fp_features, n, bad_reg) != n)
			fail(n, bad_reg);
	}

	pthread_cancel(tid);
	pthread_join(tid, NULL);

	return 0;
}
