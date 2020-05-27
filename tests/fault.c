/*
 * SPDX-License-Identifier: MIT
 *
 *
 * PURPOSE: verify that EVL properly handles user faults from the
 * out-of-band stage, which means that Dovetail passes them on as
 * expected to begin with.
 */

#include <sys/types.h>
#include <unistd.h>
#include <fenv.h>
#include <setjmp.h>
#include <stdlib.h>
#include <time.h>
#include <evl/evl.h>
#include <evl/thread.h>
#include <uapi/evl/signal.h>
#include "helpers.h"

static int *blunder, received;

static float zero;

static jmp_buf recover;

static void fault_handler(int sig, siginfo_t *si, void *context)
{
	received |= (1 << (sig-1));
	longjmp(recover, 1);
}

int main(int argc, char *argv[])
{
	struct sched_param param;
	int tfd, expected_sigs;
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = fault_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	__Tcall_assert(tfd, evl_attach_self("fault:%d", getpid()));

	expected_sigs = 1 << (SIGSEGV-1);

	if (!setjmp(recover)) {
		blunder = (int *)0x1UL;
		*blunder = 0;
	}

#ifndef __SOFTFP__	/* Right, quite ugly. */
	if (!setjmp(recover)) {
		/*
		 * If we can't enable fp exceptions, skip the divzero
		 * test.
		 */
		if (!feenableexcept(FE_DIVBYZERO)) {
			expected_sigs |= (1 << (SIGFPE-1));
			zero = 127.0 / zero;
		}
	}
#endif

	__Texpr_assert(received == expected_sigs);

	return zero;
}
