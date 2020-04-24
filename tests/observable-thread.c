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
	ssize_t ret;
	int tfd;

	__Tcall_assert(tfd, evl_attach_self("observable-thread:%d", getpid()));

	/* Subscribe to myself, should fail since not observable. */
	__Fcall_assert(ret, evl_subscribe(tfd, 16, 0));
	__Texpr_assert(ret == -EPERM);

	__Tcall_assert(ret, evl_detach_self());
	__Tcall_assert(tfd, evl_attach_thread(EVL_CLONE_OBSERVABLE,
						"observable-thread:%d", getpid()));
	/* Should work this time. */
	__Tcall_assert(ret, evl_subscribe(tfd, 16, 0));

	next.tag = EVL_NOTICE_USER;

	/* Send 1, 2, 3. */
	next.event.lval = 1ULL;
	__Tcall_errno_assert(ret, evl_update_observable(tfd, &next, 1));
	__Texpr_assert(ret == 1);
	next.event.lval = 2ULL;
	__Tcall_errno_assert(ret, evl_update_observable(tfd, &next, 1));
	__Texpr_assert(ret == 1);
	next.event.lval = 3ULL;
	__Tcall_errno_assert(ret, evl_update_observable(tfd, &next, 1));
	__Texpr_assert(ret == 1);

	/* Try sending a wrong tag, should fail. */
	next.tag = EVL_NOTICE_USER - 1;
	next.event.lval = 4ULL;
	__Fcall_assert(ret, evl_update_observable(tfd, &next, 1));
	__Texpr_assert(ret == -EINVAL);

	/* Receive the initial sequence. */
	__Tcall_assert(ret, evl_read_observable(tfd, &nf, 1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(nf.tag == EVL_NOTICE_USER);
	__Texpr_assert(nf.event.lval == 1ULL);
	__Tcall_assert(ret, evl_read_observable(tfd, &nf, 1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(nf.tag == EVL_NOTICE_USER);
	__Texpr_assert(nf.event.lval == 2ULL);
	__Tcall_assert(ret, evl_read_observable(tfd, &nf, 1));
	__Texpr_assert(ret == 1);
	__Texpr_assert(nf.tag == EVL_NOTICE_USER);
	__Texpr_assert(nf.event.lval == 3ULL);

	__Tcall_errno_assert(ret, fcntl(tfd, F_SETFL, O_NONBLOCK));
	__Fcall_assert(ret, evl_read_observable(tfd, &nf, 1));
	__Texpr_assert(ret == -EAGAIN);

	return 0;
}
