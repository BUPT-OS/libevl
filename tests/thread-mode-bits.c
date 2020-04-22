/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <evl/thread.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	struct sched_param param;
	int tfd, ret;
	int oldmask;

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);

	__Tcall_assert(tfd, evl_attach_self("thread-mode-bits:%d", getpid()));

	/* Starts with no mode bit set. */
	__Tcall_assert(ret, evl_set_thread_mode(tfd, T_WOSS|T_WOLI|T_WOSX, &oldmask));
	__Texpr_assert(oldmask == 0);
	__Tcall_assert(ret, evl_set_thread_mode(tfd, 0, &oldmask));
	__Texpr_assert(oldmask == (T_WOSS|T_WOLI|T_WOSX));
	__Tcall_assert(ret, evl_clear_thread_mode(tfd, T_WOSS, &oldmask));
	__Texpr_assert(oldmask == (T_WOSS|T_WOLI|T_WOSX));
	__Tcall_assert(ret, evl_clear_thread_mode(tfd, T_WOLI|T_WOSX, &oldmask));
	__Texpr_assert(oldmask == (T_WOLI|T_WOSX));
	__Tcall_assert(ret, evl_set_thread_mode(tfd, 0, &oldmask));
	__Texpr_assert(oldmask == 0);
	__Tcall_assert(ret, evl_set_thread_mode(tfd, 0, NULL));

	return 0;
}
