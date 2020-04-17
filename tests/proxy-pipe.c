/*
 * SPDX-License-Identifier: MIT
 *
 * PURPOSE: check that an EVL proxy properly relays the output sent by
 * an out-of-band thread through a regular pipe. The transmission path
 * is logfd->pipefd[1]->pipefd[0] where the reader receives the
 * relayed data eventually. We use a tiny buffer size to trigger the
 * buffer overflow condition often.
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

static int pipefd[2], logfd;

static void *writer(void *arg)
{
	int n, ret, tfd;
	char c;

	__Tcall_assert(tfd, evl_attach_self("pipe-writer:%d", getpid()));

	for (n = 0; n < TEST_COUNT; n++) {
		c = 'A' + (n % 26);
		__Tcall_errno_assert(ret, oob_write(logfd, &c, 1));
	}

	/* End of test. */
	c = '\0';
	__Tcall_errno_assert(ret, oob_write(logfd, &c, 1));

	return NULL;
}

int main(int argc, char *argv[])
{
	int ret, n = 0;
	pthread_t tid;
	char c, cmp;

	__Tcall_assert(ret, pipe(pipefd));
	__Tcall_assert(logfd, evl_new_proxy(pipefd[1], BUFFER_SIZE,
					"pipe-reader:%d", getpid()));
	new_thread(&tid, SCHED_FIFO, 1, writer, NULL);

	for (;;) {
		cmp = 'A' + (n++ % 26);
		ret = read(pipefd[0], &c, 1);
		if (c == '\0') {
			__Texpr_assert(n == TEST_COUNT + 1);
			break;
		}
		__Texpr_assert(c == cmp);
	}

	pthread_join(tid, NULL);

	return 0;
}
