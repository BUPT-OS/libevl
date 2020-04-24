/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <evl/thread.h>
#include <evl/observable.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	struct evl_notification nf;
	struct evl_notice next;
	int tfd, ofd;
	ssize_t ret;

	__Tcall_assert(tfd, evl_attach_self("observable-onchange:%d", getpid()));

	__Tcall_assert(ofd, evl_new_observable("observable:%d", getpid()));
	__Tcall_assert(ret, evl_subscribe(ofd, 16, EVL_NOTIFY_ONCHANGE));

	next.tag = EVL_NOTICE_USER;

	/* Send 1, 1, 2 */
	next.event.lval = 1ULL;
	__Tcall_assert(ret, evl_update_observable(ofd, &next, 1));
	__Texpr_assert(ret == 1);
	next.event.lval = 1ULL;
	__Tcall_assert(ret, evl_update_observable(ofd, &next, 1));
	__Texpr_assert(ret == 1);
	next.event.lval = 2ULL;
	__Tcall_assert(ret, evl_update_observable(ofd, &next, 1));
	__Texpr_assert(ret == 1);

	/* Receive 1, 2. */
	__Tcall_assert(ret, evl_read_observable(ofd, &nf, 1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(nf.tag == EVL_NOTICE_USER);
	__Texpr_assert(nf.event.lval == 1ULL);
	__Tcall_assert(ret, evl_read_observable(ofd, &nf, 1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(nf.tag == EVL_NOTICE_USER);
	__Texpr_assert(nf.event.lval == 2ULL);

	/* There should be nothing more to be read. */
	__Tcall_errno_assert(ret, fcntl(ofd, F_SETFL, O_NONBLOCK));
	__Fcall_assert(ret, evl_read_observable(ofd, &nf, 1));
	__Texpr_assert(ret == -EAGAIN);

	return 0;
}
