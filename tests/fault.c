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
#include <setjmp.h>
#include <stdlib.h>
#include <time.h>
#include <evl/evl.h>
#include <evl/thread.h>
#include <uapi/evl/signal.h>
#include "helpers.h"

#define EXPECTED_SIGS ((1 << (SIGSEGV-1))|(1 << (SIGFPE-1)))

static int *blunder, zero, received;

static jmp_buf recover;

static void fault_handler(int sig, siginfo_t *si, void *context)
{
	received |= (1 << (sig-1));
	longjmp(recover, 1);
}

int main(int argc, char *argv[])
{
	struct sched_param param;
	struct sigaction sa;
	int tfd;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = fault_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	__Tcall_assert(tfd, evl_attach_self("fault:%d", getpid()));

	if (!setjmp(recover)) {
		blunder = (int *)0x1UL;
		*blunder = 0;
	}

	if (!setjmp(recover))
		zero = 127 / zero;

	__Texpr_assert(received == EXPECTED_SIGS);

	return zero;
}
