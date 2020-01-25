/*
 * SPDX-License-Identifier: MIT
 *
 * Make sure we receive SIGDEBUG_STAGE_LOCKED when locked out from the
 * out-of-band stage because of a stax-based serialization.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <error.h>
#include <errno.h>
#include <evl/evl.h>
#include <uapi/evl/devices/hectic.h>
#include "helpers.h"

static int drvfd;

static volatile sig_atomic_t notified;

static struct evl_sem barrier_0, barrier_1;

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	if (sigdebug_marked(si) &&
		sigdebug_cause(si) == SIGDEBUG_STAGE_LOCKED) {
		notified = true;
		return;
	}

	evl_sigdebug_handler(sig, si, context);
	exit(1);		/* bad */
}

static void *test_thread(void *arg)
{
	int tfd, ret;
	__u32 mode;

	__Tcall_assert(tfd, evl_attach_self("stax-warn-test:%d", getpid()));
	mode = T_WOSX;
	__Tcall_errno_assert(ret, oob_ioctl(tfd, EVL_THRIOC_SET_MODE, &mode));
	__Tcall_assert(ret, evl_put_sem(&barrier_0));
	__Tcall_assert(ret, evl_get_sem(&barrier_1));

	/*
	 * In-band main() currently holds the stax, we should get
	 * SIGDEBUG here.
	 */
	ret = oob_ioctl(drvfd, EVL_HECIOC_LOCK_STAX);
	__Texpr_assert(ret == 0);

	ret = oob_ioctl(drvfd, EVL_HECIOC_UNLOCK_STAX);
	__Texpr_assert(ret == 0);

	return NULL;
}

int main(int argc, char *argv[])
{
	struct sigaction sa;
	int ret, tfd, sfd;
	pthread_t tid;
	char *name;

	drvfd = open("/dev/hectic", O_RDONLY);
	if (drvfd < 0)
		return EXIT_NO_SUPPORT;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	__Tcall_assert(tfd, evl_attach_self("stax-warn-main:%d", getpid()));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(sfd, evl_new_sem(&barrier_0, name));
	name = get_unique_name(EVL_MONITOR_DEV, 1);
	__Tcall_assert(sfd, evl_new_sem(&barrier_1, name));
	new_thread(&tid, SCHED_FIFO, 1, test_thread, NULL);

	ret = ioctl(drvfd, EVL_HECIOC_LOCK_STAX);
	if (ret) {
		if (errno == ENOTTY)
			return EXIT_NO_SUPPORT;
		__Texpr_assert(ret == 0);
	}

	__Tcall_assert(ret, evl_get_sem(&barrier_0));
	__Tcall_assert(ret, evl_put_sem(&barrier_1));
	__Tcall_assert(ret, evl_usleep(20000));

	ret = ioctl(drvfd, EVL_HECIOC_UNLOCK_STAX);
	__Texpr_assert(ret == 0);

	pthread_join(tid, NULL);

	__Texpr_assert(notified);

	return 0;
}
