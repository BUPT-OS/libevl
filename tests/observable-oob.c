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

#define MAX_THREADS 8
#define BACKLOG_DEPTH 1024

#define LOW_PRIO   1
#define HIGH_PRIO  2

static int nrthreads = 1;

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
			.lval = 0xffffffffeeeeeeeeULL,
		}
	},
};

#define NR_UPDATES	(sizeof(next_states) / sizeof(next_states[0]))

#define do_trace(__fmt, __args...)				\
	do {							\
		if (verbose)					\
			evl_printf(__fmt "\n", ##__args);	\
	} while (0)

static void usage(void)
{
        fprintf(stderr, "usage: observable-oob [options]:\n");
        fprintf(stderr, "-n --num-threads             number of observer threads\n");
        fprintf(stderr, "-l --message-loops           number of message loops\n");
        fprintf(stderr, "-v --verbose                 turn on verbosity\n");
}

#define short_optlist "vn:l:"

static const struct option options[] = {
	{
		.name = "num-threads",
		.has_arg = required_argument,
		.val = 'n',
	},
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

static void *observer_thread(void *arg)
{
	int serial = (int)(long)arg, tfd, n = 0;
	struct evl_sched_attrs attrs;
	struct evl_notification nf;
	ssize_t ret;

	do_trace("out-of-band observer #%d started", serial);

	__Tcall_assert(tfd, evl_attach_self("oob-observer:%d.%d",
						getpid(), serial));
	attrs.sched_policy = SCHED_FIFO;
	attrs.sched_priority = HIGH_PRIO;
	__Tcall_assert(ret, evl_set_schedattr(tfd, &attrs));

	__Tcall_assert(ret, evl_subscribe(observable_fd, BACKLOG_DEPTH, 0));
	__Tcall_assert(ret, evl_unsubscribe(observable_fd));
	__Tcall_assert(ret, evl_subscribe(observable_fd, BACKLOG_DEPTH, 0));

	evl_put_sem(&ready);

	for (;;) {
		ret = evl_read_observable(observable_fd, &nf, 1);
		__Texpr_assert((ret == -EBADF) || ret == 1);
		if (ret < 0)
			break;
		do_trace("[%d] msg from pid=%d, at %ld.%ld, tag=%u, state=%llx",
			serial, nf.issuer, nf.date.tv_sec, nf.date.tv_nsec,
			nf.tag, nf.event.lval);
		__Texpr_assert(next_states[n].tag == nf.tag);
		__Texpr_assert(next_states[n].event.lval == nf.event.lval);
		n = (n + 1) % NR_UPDATES;
	}

	/* Already closed in main(), should fail. */
	__Fcall_assert(ret, evl_unsubscribe(observable_fd));
	__Texpr_assert(errno == EBADF);

	do_trace("out-of-band observer #%d done", serial);

	return NULL;
}

int main(int argc, char *argv[])
{
	int tfd, ofd, n, c, loops = 1000, throttle;
	pthread_t tid[MAX_THREADS];
	struct sched_param param;
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
		case 'n':
			nrthreads = atoi(optarg);
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

	if (nrthreads <= 0)
		nrthreads = 3;
	if (nrthreads > MAX_THREADS)
		error(1, EINVAL, "max %d threads", MAX_THREADS);

	param.sched_priority = LOW_PRIO;
	__Tcall_assert(ret, pthread_setschedparam(pthread_self(), SCHED_FIFO, &param));
	__Tcall_assert(tfd, evl_attach_self("observable-oob:%d", getpid()));
	__Tcall_assert(ret, evl_new_sem(&ready, "observable-oob-ready:%d", getpid()));

	__Tcall_assert(ofd, evl_new_observable("observable:%d", getpid()));
	observable_fd = ofd;

	for (n = 0; n < nrthreads; n++) {
		new_thread(tid + n, SCHED_FIFO, 1, observer_thread, (void *)(long)n);
		__Tcall_assert(ret, evl_get_sem(&ready));
	}

	throttle = BACKLOG_DEPTH / NR_UPDATES / 2;
	for (n = 0; loops == 0 || n < loops; n++) {
		__Tcall_errno_assert(ret,
			evl_update_observable(observable_fd, next_states, NR_UPDATES));
		if (!(n % throttle))
			__Tcall_assert(ret, evl_usleep(10000));
	}

	__Tcall_assert(ret, close(observable_fd));

	for (n = 0; n < nrthreads; n++)
		pthread_join(tid[n], NULL);

	return 0;
}
