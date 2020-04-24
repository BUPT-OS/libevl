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
#include <evl/observable.h>
#include <evl/clock.h>
#include <evl/poll.h>
#include <evl/sem.h>
#include "helpers.h"

#define MAX_THREADS 8

#define LOW_PRIO   1
#define HIGH_PRIO  2

static int nrthreads = 1;

static bool verbose;

static bool send_oob;

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
};

#define NR_UPDATES	(sizeof(next_states) / sizeof(next_states[0]))

#define do_trace(__fmt, __args...)				\
	do {							\
		if (verbose)					\
			evl_printf(__fmt "\n", ##__args);	\
	} while (0)

static void usage(void)
{
        fprintf(stderr, "usage: observable-race [options]:\n");
        fprintf(stderr, "-S --send-oob             run sender out-of-band\n");
        fprintf(stderr, "-n --num-threads          number of observer threads\n");
        fprintf(stderr, "-l --message-loops        number of message loops\n");
        fprintf(stderr, "-v --verbose              turn on verbosity\n");
}

#define short_optlist "vn:Sl:"

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
		.name = "send-oob",
		.has_arg = no_argument,
		.val = 'S',
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
	int serial = (int)(long)arg, tfd, n;
	struct evl_sched_attrs attrs;
	struct evl_notification nf;
	ssize_t ret;

	do_trace("observer #%d started", serial);

	__Tcall_assert(tfd, evl_attach_self("race-observer:%d.%d",
						getpid(), serial));
	attrs.sched_policy = SCHED_FIFO;
	attrs.sched_priority = HIGH_PRIO;
	__Tcall_assert(ret, evl_set_schedattr(tfd, &attrs));

	__Tcall_assert(ret, evl_subscribe(observable_fd, 1024, 0));

	evl_put_sem(&ready);

	for (n = 0; ; n++) {
		__Tcall_assert(ret, evl_read_observable(observable_fd, &nf, 1));
		__Texpr_assert(ret == 1);
		do_trace("[%d] msg from pid=%d, at %ld.%ld, tag=%u, state=%llx",
			serial, nf.issuer, nf.date.tv_sec, nf.date.tv_nsec,
			nf.tag, nf.event.lval);
		if (!(n % 100)) {
			__Tcall_assert(ret, evl_unsubscribe(observable_fd));
			do_trace("[%d] round=%d UNSUBSCRIBED", serial, n);
			__Tcall_assert(ret, evl_subscribe(observable_fd, 16, 0));
			do_trace("[%d] round=%d SUBSCRIBED", serial, n);
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	int tfd, ofd, pfd, n, c, loops = 100000, ret;
	struct evl_poll_event pollset;
	pthread_t tid[MAX_THREADS];
	struct sched_param param;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 'S':
			send_oob = true;
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

	if (send_oob) {
		param.sched_priority = LOW_PRIO;
		__Tcall_assert(ret, pthread_setschedparam(pthread_self(), SCHED_FIFO, &param));
		do_trace("sender runs out-of-band");
	}
	__Tcall_assert(tfd, evl_attach_self("observable-race:%d", getpid()));
	__Tcall_assert(ret, evl_new_sem(&ready, "observable-race-ready:%d", getpid()));

	__Tcall_assert(ofd, evl_new_observable("observable:%d", getpid()));
	observable_fd = ofd;

	__Tcall_assert(pfd, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd, ofd, POLLOUT, evl_nil));

	for (n = 0; n < nrthreads; n++) {
		new_thread(tid + n, SCHED_FIFO, 1, observer_thread, (void *)(long)n);
		__Tcall_assert(ret, evl_get_sem(&ready));
	}

	for (n = 0; loops == 0 || n < loops; n++) {
		ret = evl_update_observable(observable_fd, next_states, NR_UPDATES);
		do_trace("round=%d wrote %d notices (errno=%d)",
			n, ret, ret < 0 ? errno : 0);
		__Texpr_assert(ret >= 0);
		if (ret == 0) {
			/*
			 * Throttle output on contention. Writability
			 * means that at least one observer has buffer
			 * space to receive at least one message.
			 */
			__Tcall_assert(ret, evl_poll(pfd, &pollset, 1));
			__Texpr_assert(ret == 1);
			__Texpr_assert(pollset.events == POLLOUT);
			__Texpr_assert(pollset.fd == ofd);
		}
	}

	for (n = 0; n < nrthreads; n++) {
		pthread_cancel(tid[n]);
		pthread_join(tid[n], NULL);
	}

	return 0;
}
