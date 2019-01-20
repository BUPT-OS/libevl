/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <evenless/evl.h>
#include <evenless/syscall.h>
#include <evenless/thread.h>
#include <evenless/logger.h>
#include <linux/types.h>
#include <uapi/evenless/control.h>
#include <uapi/evenless/signal.h>
#include "internal.h"

#define STDLOG_SIZE	32768

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static int init_status;

static struct sigaction orig_sigshadow,	orig_sigdebug;

static struct evl_core_info core_info;

int evl_ctlfd = -1;

void *evl_shared_memory = NULL;

static void atfork_unmap_shmem(void)
{
	if (evl_shared_memory) {
		munmap(evl_shared_memory, core_info.shm_size);
		evl_shared_memory = NULL;
	}

	if (evl_ctlfd >= 0) {
		close(evl_ctlfd);
		evl_ctlfd = -1;
	}

	init_once = PTHREAD_ONCE_INIT;
}

static inline int generic_init(void)
{
	int ctlfd, ret;
	void *shmem;

	ctlfd = open("/dev/evenless/control", O_RDWR);
	if (ctlfd < 0)
		return -errno;

	ret = fcntl(ctlfd, F_GETFD, 0);
	if (ret < 0) {
		ret = -errno;
		goto fail;
	}

	ret = fcntl(ctlfd, F_SETFD, ret | O_CLOEXEC);
	if (ret < 0) {
		ret = -errno;
		goto fail;
	}

	ret = ioctl(ctlfd, EVL_CTLIOC_GET_COREINFO, &core_info);
	if (ret) {
		ret = -errno;
		goto fail;
	}

	shmem = mmap(NULL, core_info.shm_size, PROT_READ|PROT_WRITE,
		     MAP_SHARED, ctlfd, 0);
	if (shmem == MAP_FAILED) {
		ret = -errno;
		goto fail;
	}

	evl_ctlfd = ctlfd;
	evl_shared_memory = shmem;

	return 0;
fail:
	close(ctlfd);

	return ret;
}

int evl_sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	if (si->si_code == SI_QUEUE &&
	    sigshadow_action(si->si_int) == SIGSHADOW_ACTION_HOME) {
		oob_ioctl(evl_ctlfd, EVL_CTLIOC_SWITCH_OOB);
		return 1;
	}

	return 0;
}

#define raw_write_out(__msg)					\
	do {							\
		int __ret;					\
		__ret = write(1, __msg , strlen(__msg));	\
		(void)__ret;					\
	} while (0)

static const char *sigdebug_msg[] = {
	"undefined\n",
	"switched inband (signal)\n",
	"switched inband (syscall)\n",
	"switched inband (fault)\n",
	"switched inband while holding mutex\n",
	"watchdog triggered\n",
	"mutex lock/unlock imbalance\n",
	"goes to sleep while holding a mutex\n",
};

void evl_sigdebug_handler(int sig, siginfo_t *si, void *ctxt)
{
	if (sigdebug_marked(si)) {
		switch (sigdebug_cause(si)) {
		case SIGDEBUG_UNDEFINED:
		case SIGDEBUG_MIGRATE_SIGNAL:
		case SIGDEBUG_MIGRATE_SYSCALL:
		case SIGDEBUG_MIGRATE_FAULT:
		case SIGDEBUG_MIGRATE_PRIOINV:
		case SIGDEBUG_WATCHDOG:
		case SIGDEBUG_MUTEX_IMBALANCE:
		case SIGDEBUG_MUTEX_SLEEP:
			raw_write_out(sigdebug_msg[sigdebug_cause(si)]);
			break;
		}
	}

	sigaction(SIGDEBUG, &orig_sigdebug, NULL);
	pthread_kill(pthread_self(), SIGDEBUG);
}

static void sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	const struct sigaction *const sa = &orig_sigshadow;
	sigset_t omask;

	if (evl_sigshadow_handler(sig, si, ctxt))
		return;

	if ((!(sa->sa_flags & SA_SIGINFO) && sa->sa_handler == NULL) ||
	    ((sa->sa_flags & SA_SIGINFO) && sa->sa_sigaction == NULL))
		return;		/* Not sent by the Evenless core */

	pthread_sigmask(SIG_SETMASK, &sa->sa_mask, &omask);

	if (!(sa->sa_flags & SA_SIGINFO))
		sa->sa_handler(sig);
	else
		sa->sa_sigaction(sig, si, ctxt);

	pthread_sigmask(SIG_SETMASK, &omask, NULL);
}

static void install_signal_handlers(void)
{
	sigset_t mask, omask;
	struct sigaction sa;

	sigemptyset(&mask);
	sigaddset(&mask, SIGSHADOW);

	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sa.sa_sigaction = sigshadow_handler;
	sigemptyset(&sa.sa_mask);
	pthread_sigmask(SIG_BLOCK, &mask, &omask);

	sigaction(SIGSHADOW, &sa, &orig_sigshadow);

	if (!(orig_sigshadow.sa_flags & SA_NODEFER))
		sigaddset(&orig_sigshadow.sa_mask, SIGSHADOW);

	pthread_sigmask(SIG_SETMASK, &omask, NULL);

	sa.sa_sigaction = evl_sigdebug_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, &orig_sigdebug);
}

static inline int do_init(void)
{
	int ret;

	ret = arch_evl_init();
	if (ret)
		return ret;

	ret = mlockall(MCL_CURRENT | MCL_FUTURE);
	if (ret)
		return -errno;

	ret = generic_init();
	if (ret)
		return ret;

	pthread_atfork(NULL, NULL, atfork_unmap_shmem);

	install_signal_handlers();

	return 0;
}

static void do_init_once(void)
{
	init_status = do_init();
}

int evl_init(void)
{
	pthread_once(&init_once, do_init_once);

	return init_status;
}

unsigned int evl_detect_fpu(void)
{
	if (evl_init())
		return 0;

	return core_info.fpu_features;
}
