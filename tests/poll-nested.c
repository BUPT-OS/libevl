/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <evenless/thread.h>
#include <evenless/clock.h>
#include <evenless/poller.h>
#include <evenless/evl.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	int tfd, pfd1, pfd2, pfd3, pfd4, pfd5;
	char *name;
	int ret;

	/* We need to be attached for calling evl_add_pollset(). */
	__Tcall_assert(tfd, evl_attach_self("poller-nested:%d", getpid()));

	/*
	 * FIXME: in this creation sequence, we can't detect too deep
	 * nesting, only cycles.
	 */
	name = get_unique_name("poller", 0);
	__Tcall_assert(pfd1, evl_new_poller(name));
	__Fcall_assert(ret, evl_add_pollset(pfd1, pfd1, POLLIN));
	__Texpr_assert(ret == -EINVAL);

	name = get_unique_name("poller", 1);
	__Tcall_assert(pfd2, evl_new_poller(name));
	__Tcall_assert(ret, evl_add_pollset(pfd1, pfd2, POLLIN));

	name = get_unique_name("poller", 2);
	__Tcall_assert(pfd3, evl_new_poller(name));
	__Tcall_assert(ret, evl_add_pollset(pfd2, pfd3, POLLIN));

	name = get_unique_name("poller", 3);
	__Tcall_assert(pfd4, evl_new_poller(name));
	__Tcall_assert(ret, evl_add_pollset(pfd3, pfd4, POLLIN));

	name = get_unique_name("poller", 4);
	__Tcall_assert(pfd5, evl_new_poller(name));
	__Tcall_assert(ret, evl_add_pollset(pfd4, pfd5, POLLIN));

	__Fcall_assert(ret, evl_add_pollset(pfd5, pfd1, POLLIN));
	__Texpr_assert(ret == -EINVAL);

	close(pfd5);
	close(pfd4);
	close(pfd3);
	close(pfd2);
	close(pfd1);

	return 0;
}
