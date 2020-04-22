/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <pthread.h>
#include <sched.h>
#include <evl/evl.h>
#include <evl/thread.h>
#include <linux/types.h>
#include <uapi/evl/factory.h>
#include <uapi/evl/control.h>
#include "internal.h"

__thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
fundle_t evl_current = EVL_NO_HANDLE;

__thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
int evl_efd = -1;

__thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
struct evl_user_window *evl_current_window;

static pthread_once_t atfork_once = PTHREAD_ONCE_INIT;

static void clear_tls(void)
{
	evl_current = EVL_NO_HANDLE;
	evl_current_window = NULL;
	evl_efd = -1;
}

static void atfork_clear_tls(void)
{
	clear_tls();
	atfork_once = PTHREAD_ONCE_INIT;
}

static void do_atfork_once(void)
{
	pthread_atfork(NULL, NULL, atfork_clear_tls);
}

int evl_attach_thread(int flags, const char *fmt, ...)
{
	int efd, ret, policy, priority;
	struct evl_sched_attrs attrs;
	struct evl_element_ids eids;
	struct sched_param param;
	va_list ap;
	char *name;

	/*
	 * Try to initialize if not yet done, so that attaching a
	 * thread to the core as the first EVL call of the process
	 * enables all services.
	 */
	ret = evl_init();
	if (ret)
		return ret;

	/*
	 * Cannot bind twice. Although the core would catch it, we can
	 * detect this issue early.
	 */
	if (evl_current != EVL_NO_HANDLE)
		return -EBUSY;

	va_start(ap, fmt);
	ret = vasprintf(&name, fmt, ap);
	va_end(ap);
	if (ret < 0)
		return -ENOMEM;

	efd = create_evl_element(EVL_THREAD_DEV, name, NULL,
				flags & EVL_CLONE_MASK, &eids);
	free(name);
	if (efd < 0)
		return efd;

	evl_current = eids.fundle;
	evl_current_window = evl_shared_memory + eids.state_offset;
	evl_efd = efd;

	/*
	 * Translate current in-band scheduling parameters to EVL
	 * scheduling attributes which we apply to self.
	 */
	ret = pthread_getschedparam(pthread_self(), &policy, &param);
	if (ret)
		goto fail;

	switch (policy) {
	case SCHED_OTHER:
	case SCHED_BATCH:
	case SCHED_IDLE:
		priority = 0;
		break;
	default:
		policy = SCHED_FIFO;
		/* Fallback wanted. */
	case SCHED_FIFO:
	case SCHED_RR:
		priority = param.sched_priority;
		break;
	}

	pthread_once(&atfork_once, do_atfork_once);

	attrs.sched_policy = policy;
	attrs.sched_priority = priority;
	ret = oob_ioctl(efd, EVL_THRIOC_SET_SCHEDPARAM, &attrs);
	if (ret) {
		ret = -errno;
		goto fail;
	}

	return efd;
fail:
	close(efd);
	clear_tls();

	return ret;
}

int evl_detach_thread(int flags)
{
	__u32 mode = T_WOSS;
	int ret;

	 /* flags are unused so far and should be zero. */
	if (flags)
		return -EINVAL;

	if (evl_current == EVL_NO_HANDLE)
		return -EPERM;

	/*
	 * Force T_WOSS off, there is no point in receiving SIGDEBUG
	 * as a result of calling ioctl() to detach from the core.
	 */
	oob_ioctl(evl_efd, EVL_THRIOC_CLEAR_MODE, &mode);

	ret = ioctl(evl_efd, EVL_THRIOC_DETACH_SELF);
	if (ret)
		return -errno;

	close(evl_efd);
	clear_tls();

	return 0;
}

int evl_get_self(void)
{
	return evl_efd;
}

int evl_switch_oob(void)
{
	int ret;

	if (evl_current == EVL_NO_HANDLE)
		return -EPERM;

	if (!evl_is_inband())
		return 0;

	ret = oob_ioctl(evl_efd, EVL_THRIOC_SWITCH_OOB);

	return ret ? -errno : 0;
}

int evl_switch_inband(void)
{
	int ret;

	if (evl_is_inband())
		return 0;

	if (evl_current == EVL_NO_HANDLE)
		return -EPERM;

	ret = ioctl(evl_efd, EVL_THRIOC_SWITCH_INBAND);

	return ret ? -errno : 0;
}

int evl_get_state(int efd, struct evl_thread_state *statebuf)
{
	return __evl_common_ioctl(efd, EVL_THRIOC_GET_STATE, statebuf);
}

int evl_unblock_thread(int efd)
{
	return __evl_common_ioctl(efd, EVL_THRIOC_UNBLOCK);
}

int evl_demote_thread(int efd)
{
	return __evl_common_ioctl(efd, EVL_THRIOC_DEMOTE);
}

static int do_thread_mode(int efd, int op, int mask, int *oldmask)
{
	__u32 val = mask;
	int ret;

	ret = __evl_common_ioctl(efd, op, &val);
	if (ret)
		return ret;

	if (oldmask)
		*oldmask = val;

	return 0;
}

int evl_set_thread_mode(int efd, int mask, int *oldmask)
{
	return do_thread_mode(efd, EVL_THRIOC_SET_MODE, mask, oldmask);
}

int evl_clear_thread_mode(int efd, int mask, int *oldmask)
{
	return do_thread_mode(efd, EVL_THRIOC_CLEAR_MODE, mask, oldmask);
}
