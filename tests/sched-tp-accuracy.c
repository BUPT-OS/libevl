/*
 * SPDX-License-Identifier: MIT
 *
 * Adapted from Xenomai smokey/sched-tp test (https://xenomai.org/)
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 * Relicensed by its author from GPLv2 to MIT.
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <evl/evl.h>
#include "helpers.h"

#define NR_WINDOWS  4

#define do_trace(__fmt, __args...)				\
	do {							\
		if (verbose)					\
			evl_printf(__fmt "\n", ##__args);	\
	} while (0)

#define do_warn(__fmt, __args...)	\
	fprintf(stderr, __fmt "\n", ##__args)

static pthread_t threadA, threadB, threadC;

/*
 * No static init here, we have multiple threads competing on first
 * access.
 */
static struct evl_sem barrier;

static struct evl_mutex lock;

static const char ref_schedule[] =
	"CCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCC"
	"BBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAA"
	"CCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCC"
	"BBBBBAACCCCCCCCCCBBBBBAACCCCCCCCCCBBBBBAACCCCCCCC";

static char schedule[sizeof(ref_schedule) + 8], *curr = schedule;

static int overflow;

static int verbose;

static void usage(void)
{
        fprintf(stderr, "usage: heap-torture [options]:\n");
        fprintf(stderr, "-s --sequential-test-size    heap size for sequential access tests\n");
        fprintf(stderr, "-p --pattern-test-size       heap size for pattern tests\n");
        fprintf(stderr, "-c --pattern-check-rounds    number of pattern tests\n");
        fprintf(stderr, "-r --random-check-rounds     number of random allocation tests\n");
        fprintf(stderr, "-v --verbose                 turn on verbosity\n");
}

#define short_optlist "v"

static const struct option options[] = {
	{
		.name = "verbose",
		.has_arg = no_argument,
		.val = 'v',
	},
	{ /* Sentinel */ }
};

static void *tp_thread(void *arg)
{
	int ret, part = (int)(long)arg, efd;
	struct evl_sched_attrs attrs;

	__Tcall_assert(efd, evl_attach_self("sched-tp-accuracy:%d.%d",
						getpid(), part));
	attrs.sched_policy = SCHED_TP;
	attrs.sched_priority = 50 - part;
	attrs.sched_tp_partition = part;
	__Tcall_assert(ret, evl_set_schedattr(efd, &attrs));

	__Tcall_assert(ret, evl_get_sem(&barrier));
	__Tcall_assert(ret, evl_put_sem(&barrier));

	for (;;) {
		/*
		 * The mutex would protect us against inconsistent
		 * scheduler behavior so that we don't write out of
		 * bounds; otherwise no serialization should happen
		 * due to this lock.
		 */
		__Tcall_assert(ret, evl_lock_mutex(&lock));
		if (curr >= schedule + sizeof(schedule)) {
			__Tcall_assert(ret, evl_unlock_mutex(&lock));
			overflow = 1;
			break;
		}
		*curr++ = 'A' + part;
		__Tcall_assert(ret, evl_unlock_mutex(&lock));
		__Tcall_assert(ret, evl_usleep(10500));
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	union evl_sched_ctlparam *p;
	union evl_sched_ctlinfo *q;
	cpu_set_t affinity;
	int ret, n, c;
	size_t len;
	char *s;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 'v':
			verbose = 1;
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

	__Tcall_assert(ret, evl_init());
	__Tcall_assert(ret, evl_new_sem(&barrier, "tp-sem:%d", getpid()));
	__Tcall_assert(ret, evl_new_mutex(&lock, "tp-mutex:%d", getpid()));

	/*
	 * We need all threads to be pinned on CPU0, so that
	 * evl_set_schedattr() implicitly refers to the TP schedule we
	 * are about to install on that particular CPU.
	 */
	CPU_ZERO(&affinity);
	CPU_SET(0, &affinity);
	__Tcall_assert(ret, sched_setaffinity(0, sizeof(affinity), &affinity));

	/*
	 * For a recurring global time frame of 400 ms, we define a TP
	 * schedule as follows:
	 *
	 * - thread(s) assigned to partition #2 (tag C) shall be
	 * allowed to run for 100 ms, when the next global time frame
	 * begins.
	 *
	 * - thread(s) assigned to partition #1 (tag B) shall be
	 * allowed to run for 50 ms, after the previous time slot
	 * ends.
	 *
	 * - thread(s) assigned to partition #0 (tag A) shall be
	 * allowed to run for 20 ms, after the previous time slot
	 * ends.
	 *
	 * - when the previous time slot ends, no TP thread shall be
	 * allowed to run until the global time frame ends (special
	 * setting of ptid == EVL_TP_IDLE), i.e. 230 ms.
	 */

 	/* Assign the TP schedule to CPU0. */
	len = evl_tp_paramlen(NR_WINDOWS);
	p = malloc(len);
	if (p == NULL)
		error(1, ENOMEM, "malloc");

	p->tp.op = evl_tp_install;
	p->tp.nr_windows = NR_WINDOWS;
	p->tp.windows[0].offset.tv_sec = 0;
	p->tp.windows[0].offset.tv_nsec = 0;
	p->tp.windows[0].duration.tv_sec = 0;
	p->tp.windows[0].duration.tv_nsec = 100000000;
	p->tp.windows[0].ptid = 2;
	p->tp.windows[1].offset.tv_sec = 0;
	p->tp.windows[1].offset.tv_nsec = 100000000;
	p->tp.windows[1].duration.tv_sec = 0;
	p->tp.windows[1].duration.tv_nsec = 50000000;
	p->tp.windows[1].ptid = 1;
	p->tp.windows[2].offset.tv_sec = 0;
	p->tp.windows[2].offset.tv_nsec = 150000000;
	p->tp.windows[2].duration.tv_sec = 0;
	p->tp.windows[2].duration.tv_nsec = 20000000;
	p->tp.windows[2].ptid = 0;
	p->tp.windows[3].offset.tv_sec = 0;
	p->tp.windows[3].offset.tv_nsec = 170000000;
	p->tp.windows[3].duration.tv_sec = 0;
	p->tp.windows[3].duration.tv_nsec = 230000000;
	p->tp.windows[3].ptid = EVL_TP_IDLE;
	ret = evl_sched_control(SCHED_TP, p, NULL, 0);
	if (ret == -EOPNOTSUPP)
		return EXIT_NO_SUPPORT;
	__Texpr_assert(ret == 0);

	/* Then query the settings back. */
	len = evl_tp_infolen(NR_WINDOWS);
	q = malloc(len);
	if (q == NULL)
		error(1, ENOMEM, "malloc");

	p->tp.op = evl_tp_get;
	p->tp.nr_windows = NR_WINDOWS;
	__Tcall_assert(ret, evl_sched_control(SCHED_TP, p, q, 0));

	do_trace("check: %d windows", q->tp.nr_windows);
	for (n = 0; n < 4; n++)
		do_trace("[%d] offset = { %ld s, %ld ns }, duration = { %ld s, %ld ns }, ptid = %d",
			n,
			q->tp.windows[n].offset.tv_sec,
			q->tp.windows[n].offset.tv_nsec,
			q->tp.windows[n].duration.tv_sec,
			q->tp.windows[n].duration.tv_nsec,
			q->tp.windows[n].ptid);
	free(q);

	new_thread(&threadA, SCHED_FIFO, 1, tp_thread, (void *)0L);
	new_thread(&threadB, SCHED_FIFO, 1, tp_thread, (void *)1L);
	new_thread(&threadC, SCHED_FIFO, 1, tp_thread, (void *)2L);

	/* Start the TP schedule. */
	p->tp.op = evl_tp_start;
	__Tcall_assert(ret, evl_sched_control(SCHED_TP, p, NULL, 0));
	free(p);

	__Tcall_assert(ret, evl_put_sem(&barrier));

	do_trace("running for 3s");
	sleep(3);	/* Run for a while. */

	pthread_cancel(threadC);
	pthread_cancel(threadB);
	pthread_cancel(threadA);
	pthread_join(threadC, NULL);
	pthread_join(threadB, NULL);
	pthread_join(threadA, NULL);

	if (getenv("EVL_IN_VM"))
		return 0;

	if (overflow) {
		do_warn("schedule overflowed");
		return 1;
	}

	/*
	 * The first time window might be decreased for enough time to
	 * skip an iteration due to lingering inits, and a few more
	 * marks may be generated while we are busy stopping the
	 * threads, so we look for a valid sub-sequence.
	 */
	s = strstr(ref_schedule, schedule);
	if (s == NULL || s - ref_schedule > 1) {
		do_warn("unexpected schedule:\n%s", schedule);
		return 2;
	}

	return 0;
}
