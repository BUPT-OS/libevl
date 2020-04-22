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

	__Tcall_assert(tfd, evl_attach_self("stax-warn-test:%d", getpid()));
	__Tcall_assert(ret, evl_set_thread_mode(tfd, T_WOSX, NULL));

	/*
	 * In-band main() currently holds the stax, we should get
	 * SIGDEBUG as a result of issuing a lock request.
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
	pthread_t tid;
	int ret;

	/*
	 * CAUTION: this test uses an internal interface of the
	 * 'hectic' driver in order to test the stax mechanism. This
	 * interface is enabling the caller to do something wrong and
	 * nasty, i.e. holding a stax across the kernel/user space
	 * boundary. This is only for the purpose of testing this
	 * mechanism, this is bad, applications should never do this,
	 * ever. IOW, a stax should be held while in kernel space
	 * exclusively, always released before returning to user.
	 */
	drvfd = open("/dev/hectic", O_RDONLY);
	if (drvfd < 0)
		return EXIT_NO_SUPPORT;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	ret = ioctl(drvfd, EVL_HECIOC_LOCK_STAX);
	if (ret) {
		if (errno == ENOTTY)
			return EXIT_NO_SUPPORT;
		__Texpr_assert(ret == 0);
	}

	new_thread(&tid, SCHED_FIFO, 1, test_thread, NULL);

	/* Wait for the oob thread to try locking the stax. */
	sleep(1);

	ret = ioctl(drvfd, EVL_HECIOC_UNLOCK_STAX);
	__Texpr_assert(ret == 0);

	pthread_join(tid, NULL);

	__Texpr_assert(notified);

	return 0;
}
