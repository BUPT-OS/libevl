/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <evl/thread.h>
#include <evl/sched.h>
#include <evl/clock.h>
#include <evl/observable.h>
#include <evl/sem.h>
#include "helpers.h"

#define MAX_THREADS 5
#define BACKLOG_DEPTH 1024

#define HIGH_PRIO  2

static bool verbose;

static int observable_fd;

static struct evl_sem ready;

static struct evl_notice next_states[] = {
	[0] = {
		.tag = EVL_NOTICE_USER,
		.event = {
			.lval = 0xa1a2a3a4a5bbccddULL,
		},
	},
	[1] = {
		.tag = EVL_NOTICE_USER + 1,
		.event = {
			.lval = 0xb1b2b3b4b5ffeeddULL,
		},
	},
	[2] = {
		.tag = EVL_NOTICE_USER + 2,
		.event = {
			.lval = 0xc1c2c3c4c5ccaabbULL,
		}
	},
	[3] = {
		.tag = EVL_NOTICE_USER + 3,
		.event = {
			.lval = 0xd1d2d3d4d5ffeeddULL,
		}
	},
	[4] = {
		.tag = EVL_NOTICE_USER + 4,
		.event = {
			.lval = 0xe1e2e3e4e5010203ULL,
		}
	},
};

#define do_trace(__fmt, __args...)				\
	do {							\
		if (verbose)					\
			evl_printf(__fmt "\n", ##__args);	\
	} while (0)

static void usage(void)
{
        fprintf(stderr, "usage: observable-master [options]:\n");
        fprintf(stderr, "-l --message-loops           number of message loops\n");
        fprintf(stderr, "-v --verbose                 turn on verbosity\n");
}

#define short_optlist "vl:"

static const struct option options[] = {
	{
		.name = "message-loops",
		.has_arg = required_argument,
		.val = 'l',
	},
	{
		.name = "verbose",
		.has_arg = no_argument,
		.val = 'v',
	},
	{ /* Sentinel */ }
};

static void *worker_thread(void *arg)
{
	int serial = (int)(long)arg, tfd;
	struct evl_notification nf;
	ssize_t ret;

	do_trace("worker thread #%d started", serial);

	__Tcall_assert(tfd, evl_attach_self("oob-observer:%d.%d",
						getpid(), serial));

	__Tcall_assert(ret, evl_subscribe(observable_fd, BACKLOG_DEPTH, 0));

	evl_put_sem(&ready);

	/*
	 * Expect round-robin scheduling of workers if master mode.
	 */
	for (;;) {
		ret = evl_read_observable(observable_fd, &nf, 1);
		__Texpr_assert((ret == -EBADF) || ret == 1);
		if (ret < 0)
			break;
		do_trace("[%d] msg from pid=%d, at %ld.%ld, tag=%u, state=%llx",
			serial, nf.issuer, nf.date.tv_sec, nf.date.tv_nsec,
			nf.tag, nf.event.lval);
		__Texpr_assert(next_states[serial].tag == nf.tag);
		__Texpr_assert(next_states[serial].event.lval == nf.event.lval);
	}

	/* Already closed in main(), should fail. */
	__Fcall_assert(ret, evl_unsubscribe(observable_fd));
	__Texpr_assert(errno == EBADF);

	do_trace("worker thread #%d done", serial);

	return NULL;
}

int main(int argc, char *argv[])
{
	int tfd, ofd, n, c, loops = 1000;
	pthread_t tid[MAX_THREADS];
	ssize_t ret;

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
		case 'l':
			loops = atoi(optarg);
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

	__Tcall_assert(tfd, evl_attach_self("observable-master:%d", getpid()));
	__Tcall_assert(ret, evl_new_sem(&ready, "observable-master-ready:%d", getpid()));

	__Tcall_assert(ofd, evl_create_observable(EVL_CLONE_MASTER,
					"observable:%d", getpid()));
	observable_fd = ofd;

	for (n = 0; n < MAX_THREADS; n++) {
		new_thread(tid + n, SCHED_FIFO, HIGH_PRIO, worker_thread, (void *)(long)n);
		__Tcall_assert(ret, evl_get_sem(&ready));
	}

	for (n = 0; loops == 0 || n < loops; n++)
		__Tcall_errno_assert(ret, evl_update_observable(observable_fd,
					&next_states[n % MAX_THREADS], 1));

	__Tcall_assert(ret, close(observable_fd));

	for (n = 0; n < MAX_THREADS; n++)
		pthread_join(tid[n], NULL);

	return 0;
}
