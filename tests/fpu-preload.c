/*
 * SPDX-License-Identifier: MIT
 *
 *
 * PURPOSE: check that the FPU state is preloaded in the hardware as
 * expected when an EVL thread emerges, including when the attachement
 * follows a plain fork() [and no exec()]. If that action unexpectedly
 * triggers a FPU trap for which a transition to inband mode is
 * required, we would be notified via the SIGDEBUG handler (cause ==
 * EVL_HMDIAG_EXDEMOTE).
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <evl/evl.h>
#include <evl/thread.h>
#include <uapi/evl/signal.h>
#include "helpers.h"

static double get_float(void)
{
	return 2277.55 * (double)rand();
}

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	evl_sigdebug_handler(sig, si, context);
	exit(1);		/* bad */
}

int main(int argc, char *argv[])
{
	struct sched_param param;
	struct sigaction sa;
	int tfd, ret;
	double f;

	srand(time(NULL));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	__Tcall_assert(tfd, evl_attach_self("fpu-preload:%d", getpid()));

	__Tcall_assert(ret, evl_set_thread_mode(tfd, T_WOSS, NULL));
	f = get_float() * get_float();
	__Tcall_assert(ret, evl_clear_thread_mode(tfd, T_WOSS, NULL));
	if (fork() == 0) {
		__Tcall_assert(tfd, evl_attach_self("fpu-preload-child:%d", getpid()));
		__Tcall_assert(ret, evl_set_thread_mode(tfd, T_WOSS, NULL));
		f = get_float() * get_float();
	}

	return f == NAN;
}
