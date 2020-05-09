/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>
#include <evl/proxy.h>
#include <evl/syscall.h>
#include "helpers.h"

#define TEST_COUNT  2048
#define BUFFER_SIZE 8

static int p2m_pipefd[2], p2m_proxy;

static int m2p_pipefd[2], m2p_proxy;

static void *peer(void *arg)
{
	int n = 0, ret, tfd;
	char c, cmp;

	__Tcall_assert(tfd, evl_attach_self("pipe-io-peer:%d", getpid()));

	for (;;) {
		cmp = c = 'A' + (n % 26);
		__Tcall_errno_assert(ret, oob_write(p2m_proxy, &c, 1));
		__Tcall_errno_assert(ret, oob_read(m2p_proxy, &c, 1));
		if (ret == 0) {	/* End of test. */
			__Texpr_assert(n == TEST_COUNT);
			break;
		}
		__Texpr_assert(c == cmp);
		n++;
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t tid;
	char c, cmp;
	int ret, n;

	__Tcall_assert(ret, pipe(p2m_pipefd));
	__Tcall_assert(ret, pipe(m2p_pipefd));
	__Tcall_assert(m2p_proxy, evl_create_proxy(m2p_pipefd[0], BUFFER_SIZE,
						0, EVL_CLONE_INPUT,
						"pipe-m2p:%d", getpid()));
	__Tcall_assert(p2m_proxy, evl_create_proxy(p2m_pipefd[1], BUFFER_SIZE,
						0, EVL_CLONE_OUTPUT,
						"pipe-p2m:%d", getpid()));
	new_thread(&tid, SCHED_FIFO, 1, peer, NULL);

	for (n = 0; n < TEST_COUNT; n++) {
		cmp = 'A' + (n % 26);
		ret = read(p2m_pipefd[0], &c, 1);
		__Texpr_assert(c == cmp);
		/* Send the character back. */
		__Tcall_errno_assert(ret, write(m2p_pipefd[1], &c, 1));
	}

	__Tcall_errno_assert(ret, close(m2p_pipefd[1]));

	pthread_join(tid, NULL);

	return 0;
}
