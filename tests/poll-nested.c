/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <evl/thread.h>
#include <evl/clock.h>
#include <evl/poll.h>
#include <evl/evl.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	int tfd, pfd1, pfd2, pfd3, pfd4, pfd5;
	int ret;

	/* We need to be attached for calling evl_add_pollset(). */
	__Tcall_assert(tfd, evl_attach_self("poller-nested:%d", getpid()));

	/*
	 * FIXME: in this creation sequence, we can't detect too deep
	 * nesting, only cycles.
	 */
	__Tcall_assert(pfd1, evl_new_poll());
	__Fcall_assert(ret, evl_add_pollfd(pfd1, pfd1, POLLIN, evl_nil));
	__Texpr_assert(ret == -ELOOP);

	__Tcall_assert(pfd2, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd1, pfd2, POLLIN, evl_nil));

	__Tcall_assert(pfd3, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd2, pfd3, POLLIN, evl_nil));

	__Tcall_assert(pfd4, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd3, pfd4, POLLIN, evl_nil));

	__Tcall_assert(pfd5, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd4, pfd5, POLLIN, evl_nil));

	__Fcall_assert(ret, evl_add_pollfd(pfd5, pfd1, POLLIN, evl_nil));
	__Texpr_assert(ret == -ELOOP);

	close(pfd5);
	close(pfd4);
	close(pfd3);
	close(pfd2);
	close(pfd1);

	return 0;
}
