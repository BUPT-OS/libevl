/*
 * SPDX-License-Identifier: MIT
 *
 * Adapted from Xenomai smokey/sched-quota test (https://xenomai.org/)
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
#include <pthread.h>
#include <math.h>
#include <evl/evl.h>
#include "helpers.h"

#define do_trace(__fmt, __args...)				\
	do {							\
		if (verbose)					\
			evl_printf(__fmt "\n", ##__args);	\
	} while (0)

#define MAX_THREADS 8
#define TEST_SECS   1

static unsigned long long crunch_per_sec, loops_per_sec;

static struct quota_thread_desc {
	int efd;
	pthread_t tid;
	struct evl_sched_attrs attrs;
	unsigned long count;
} threads[MAX_THREADS];

static int nrthreads = 3;

/* Default to 10% of the CPU bandwidth. */
static int quota = 10;

static int tgid = -1;

static struct evl_event barrier;

static struct evl_mutex lock;

static bool verbose;

/*
 * Try CPU1 first, since we may have a lot of in-band stuff going on
 * CPU0 which we may not want to stall too much while testing
 * (e.g. IRQ handling).
 */
static int test_cpu = 1;

static bool started;

static volatile sig_atomic_t done;

static struct evl_sem ready;

static void usage(void)
{
        fprintf(stderr, "usage: sched-quota-accuracy [options]:\n");
        fprintf(stderr, "-c --cpu                     run test on given CPU [=1]\n");
        fprintf(stderr, "-n --num-threads             number of sibling threads\n");
        fprintf(stderr, "-v --verbose                 turn on verbosity\n");
}

#define short_optlist "vn:c:"

static const struct option options[] = {
	{
		.name = "num-threads",
		.has_arg = required_argument,
		.val = 'n',
	},
	{
		.name = "cpu",
		.has_arg = required_argument,
		.val = 'c',
	},
	{
		.name = "verbose",
		.has_arg = no_argument,
		.val = 'v',
	},
	{ /* Sentinel */ }
};

static unsigned long __attribute__(( noinline ))
__do_work(unsigned long count)
{
	return count + 1;
}

static void __attribute__(( noinline ))
do_work(unsigned long loops, unsigned long *count_r)
{
	unsigned long n;

	for (n = 0; n < loops; n++)
		*count_r = __do_work(*count_r);
}

static void set_thread_affinity(void)
{
	cpu_set_t affinity;
	int ret;

	CPU_ZERO(&affinity);
	CPU_SET(test_cpu, &affinity);
	__Tcall_assert(ret, sched_setaffinity(0, sizeof(affinity), &affinity));
}

static void *quota_thread(void *arg)
{
	int serial = (int)(long)arg, ret;
	struct quota_thread_desc *t = threads + serial;
	unsigned long loops;

	set_thread_affinity();

	loops = crunch_per_sec / 100; /* yield each 10 ms runtime */
	t->count = 0;

	__Tcall_assert(t->efd, evl_attach_self("sched-quota-accuracy:%d.%d",
						getpid(), serial));
	__Tcall_assert(ret, evl_set_schedattr(t->efd, &threads[serial].attrs));

	evl_put_sem(&ready);

	__Tcall_assert(ret, evl_lock_mutex(&lock));
	for (;;) {
		if (started)
			break;
		__Tcall_assert(ret, evl_wait_event(&barrier, &lock));
	}
	__Tcall_assert(ret, evl_unlock_mutex(&lock));

	while (!done) {
		do_work(loops, &t->count);
		if (nrthreads > 1)
			__Tcall_assert(ret, evl_yield());
	}

	/*
	 * Each thread is instantiated twice in the course of the
	 * test, so make sure to detach in order to have the thread
	 * elements released between runs.
	 */
	evl_detach_self();

	return NULL;
}

static void create_quota_thread(int tgid, int serial)
{
	struct quota_thread_desc *t = threads + serial;

	t->attrs.sched_policy = SCHED_QUOTA;
	t->attrs.sched_priority = 1;
	t->attrs.sched_quota_group = tgid;
	new_thread(&t->tid, SCHED_FIFO, 1, quota_thread, (void *)(long)serial);
}

static void create_fifo_thread(int serial)
{
	struct quota_thread_desc *t = threads + serial;

	t->attrs.sched_policy = SCHED_FIFO;
	t->attrs.sched_priority = 1;
	new_thread(&t->tid, SCHED_FIFO, 1, quota_thread, (void *)(long)serial);
}

static int cleanup_group(void)
{
	union evl_sched_ctlparam p;

	p.quota.op = evl_quota_remove;
	p.quota.u.remove.tgid = tgid;
	return evl_control_sched(SCHED_QUOTA, &p, NULL, test_cpu);
}

static void cleanup_group_on_sig(int sig, siginfo_t *si, void *context)
{
	if (tgid != -1)
		cleanup_group();
}

static double run_quota(int quota)
{
	union evl_sched_ctlparam p;
	union evl_sched_ctlinfo q;
	unsigned long long count;
	struct sigaction sa;
	struct timespec ts;
	double percent;
	int ret, n;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = cleanup_group_on_sig;
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	atexit((void (*)(void))cleanup_group);

	p.quota.op = evl_quota_add;
	__Tcall_assert(ret, evl_control_sched(SCHED_QUOTA, &p, &q, test_cpu));

	p.quota.op = evl_quota_set;
	p.quota.u.set.tgid = q.quota.tgid;
	p.quota.u.set.quota = quota;
	p.quota.u.set.quota_peak = quota;
	__Tcall_assert(ret, evl_control_sched(SCHED_QUOTA, &p, &q, test_cpu));
	tgid = q.quota.tgid;

	do_trace("CPU%d: new thread group #%d, quota sum is %d%%",
		test_cpu, tgid, q.quota.quota_sum);

	for (n = 0; n < nrthreads; n++) {
		create_quota_thread(tgid, n);
		__Tcall_assert(ret, evl_get_sem(&ready));
	}

	__Tcall_assert(ret, evl_lock_mutex(&lock));
	started = true;
	__Tcall_assert(ret, evl_broadcast_event(&barrier));
	__Tcall_assert(ret, evl_unlock_mutex(&lock));

	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &ts));
	ts.tv_sec += TEST_SECS;
	__Tcall_assert(ret, evl_sleep_until(EVL_CLOCK_MONOTONIC, &ts));

	done = true;

	for (n = 0, count = 0; n < nrthreads; n++) {
		count += threads[n].count;
		__Tcall_assert(ret, evl_demote_thread(threads[n].efd));
	}

	percent = ((double)count / TEST_SECS) * 100.0 / loops_per_sec;

	for (n = 0; n < nrthreads; n++) {
		do_trace("CPU%d: done quota_thread[%d], count=%lu",
			test_cpu, n, threads[n].count);
		pthread_join(threads[n].tid, NULL);
	}

	__Tcall_assert(ret, cleanup_group());

	return percent;
}

static void timespec_sub(struct timespec *__restrict r,
		  const struct timespec *__restrict t1,
		  const struct timespec *__restrict t2)
{
	r->tv_sec = t1->tv_sec - t2->tv_sec;
	r->tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

static unsigned long long calibrate(void)
{
	struct timespec start, end, delta;
	const int crunch_loops = 10000;
	unsigned long long ns, lps;
	unsigned long count;
	struct timespec ts;
	int n, ret;

	count = 0;
	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &start));
	do_work(crunch_loops, &count);
	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &end));
	/* Give in-band a breath between long runs. */
	__Tcall_assert(ret, evl_usleep(2000));

	timespec_sub(&delta, &end, &start);
	ns = delta.tv_sec * ONE_BILLION + delta.tv_nsec;
	crunch_per_sec = (unsigned long long)((double)ONE_BILLION / (double)ns * crunch_loops);

	for (n = 0; n < nrthreads; n++) {
		create_fifo_thread(n);
		__Tcall_assert(ret, evl_get_sem(&ready));
	}

	__Tcall_assert(ret, evl_lock_mutex(&lock));
	started = true;
	__Tcall_assert(ret, evl_broadcast_event(&barrier));
	__Tcall_assert(ret, evl_unlock_mutex(&lock));

	__Tcall_assert(ret, evl_read_clock(EVL_CLOCK_MONOTONIC, &ts));
	ts.tv_sec += TEST_SECS;
	__Tcall_assert(ret, evl_sleep_until(EVL_CLOCK_MONOTONIC, &ts));

	done = true;

	for (n = 0, lps = 0; n < nrthreads; n++) {
		lps += threads[n].count;
		__Tcall_assert(ret, evl_demote_thread(threads[n].efd));
	}

	for (n = 0; n < nrthreads; n++)
		__Tcall_assert(ret, pthread_join(threads[n].tid, NULL));

	started = false;
	done = false;
	__Tcall_assert(ret, evl_usleep(2000));

	return lps;
}

int main(int argc, char *argv[])
{
	struct sched_param param;
	double effective;
	int ret, c;

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
		case 'n':
			nrthreads = atoi(optarg);
			break;
		case 'c':
			test_cpu = atoi(optarg);
			if (test_cpu < 0 || test_cpu >= CPU_SETSIZE)
				error(1, EINVAL, "invalid CPU number");
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

	param.sched_priority = 50;
	__Tcall_assert(ret, pthread_setschedparam(pthread_self(), SCHED_FIFO, &param));
	__Tcall_assert(ret, evl_attach_self("sched-quota-accuracy:%d", getpid()));

	__Tcall_assert(ret, evl_new_sem(&ready, "quota-ready:%d", getpid()));
	__Tcall_assert(ret, evl_new_event(&barrier, "quota-barrier:%d", getpid()));
	__Tcall_assert(ret, evl_new_mutex(&lock, "quota-lock:%d", getpid()));

	/*
	 * We need all threads to be pinned on the same (oob-capable)
	 * CPU, so that evl_set_schedattr() implicitly refers to the
	 * quota settings we are about to install on that particular
	 * CPU.
	 */
	test_cpu = pick_test_cpu(test_cpu, false, NULL);
	do_trace("picked CPU%d for execution", test_cpu);
	set_thread_affinity();

	if (nrthreads <= 0)
		nrthreads = 3;
	if (nrthreads > MAX_THREADS)
		error(1, EINVAL, "max %d threads", MAX_THREADS);

	calibrate();	/* Warming up, ignore result. */
	loops_per_sec = calibrate();

	do_trace("CPU%d: calibrating: %Lu loops/sec", test_cpu, loops_per_sec);

	effective = run_quota(quota);

	do_trace("CPU%d: %d thread%s: cap=%d%%, effective=%.1f%%",
		test_cpu, nrthreads, nrthreads > 1 ? "s": "", quota, effective);

	/* Tolerate 1.5% variation, give or take. */
	if (!getenv("EVL_IN_VM") && fabs(effective - (double)quota) > 1.5) {
		do_trace("CPU%d: out of quota: %.1f%%",
			test_cpu, effective - (double)quota);
		return 2;
	}

	return 0;
}
