/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <evl/thread.h>
#include <evl/observable.h>
#include "helpers.h"

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	__Texpr_assert(0);	/* Bummer. */
}

int main(int argc, char *argv[])
{
	struct __evl_notification _nf;
	struct evl_notification nf;
	struct sched_param param;
	struct sigaction sa;
	int oldmask, tfd;
	ssize_t ret;

	/* Install a handler for a signal we don't want to receive. */
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	__Tcall_assert(tfd, evl_attach_thread(EVL_CLONE_OBSERVABLE|EVL_CLONE_NONBLOCK,
						"observable-hm:%d", getpid()));

	__Fcall_assert(ret, evl_read_observable(tfd, &nf, 1));
	__Texpr_assert(ret == -ENXIO);

	/* Introspection: monitor ourselves for HM events. */
	__Tcall_assert(ret, evl_subscribe(tfd, 16, 0));

	/* Enable stage switch notifications only via observable. */
	__Tcall_assert(ret, evl_set_thread_mode(tfd, T_WOSS|T_HMOBS, &oldmask));
	/*
	 * Starts with no mode bit set, except maybe T_WOLI if
	 * CONFIG_EVL_DEBUG_WOLI is enabled.
	 */
	__Texpr_assert((oldmask & ~T_WOLI) == 0);

	/*
	 * We are still in-band in the wake of evl_subscribe(), switch
	 * oob. Don't do that in your apps, this is most often useless
	 * since the core switches threads to the right stage. We need
	 * that only for the purpose of checking how the core behaves.
	 */
	__Tcall_assert(ret, evl_switch_oob());

	/*
	 * Hack: we ask for the next pending notification from our HM
	 * queue using an in-band read() syscall, which should switch
	 * us in-band and trigger the notification we expect about
	 * having been demoted in the same move. We can only do that
	 * via the internal interface, since evl_read_observable()
	 * would use oob_read() instead. For the purpose of testing
	 * only, you should you really not do that in your apps.
	 */
	__Tcall_errno_assert(ret, read(tfd, &_nf, sizeof(_nf)));
	__Texpr_assert(_nf.tag == EVL_HMDIAG_SYSDEMOTE);

	/* Nothing else afterwards. */
	__Fcall_assert(ret, evl_read_observable(tfd, &nf, 1));
	__Texpr_assert(ret == -EAGAIN);

	return 0;
}
