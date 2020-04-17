/*
 * SPDX-License-Identifier: MIT
 *
 * PURPOSE: connect a proxy to an eventfd, checking that write
 * with fixed granularity works.
 */

#include <sys/types.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>
#include <evl/proxy.h>
#include <evl/syscall.h>
#include "helpers.h"

#define TEST_COUNT  2000

static int evntfd, proxyfd;

static void *writer(void *arg)
{
	int n, ret, tfd;
	uint64_t val;

	__Tcall_assert(tfd, evl_attach_self("event-writer:%d", getpid()));

	/* We should receive EINVAL for a truncated write. */
	__Fcall_assert(ret, oob_write(proxyfd, &val, sizeof(val) / 2));
	__Texpr_assert(errno == EINVAL);

	for (n = 0; n < TEST_COUNT; n++) {
		val = 1;
		__Tcall_assert(ret, oob_write(proxyfd, &val, sizeof(val)));
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	int ret, n = 0;
	pthread_t tid;
	uint64_t val;

	__Tcall_assert(evntfd, eventfd(0, EFD_SEMAPHORE));
	/*
	 * 64-bit write granularity, up to 3 buffered values before
	 * oob_write() blocks waiting for the output to drain.
	 */
	__Tcall_assert(proxyfd, evl_create_proxy(evntfd, sizeof(uint64_t) * 3,
						sizeof(uint64_t), 0,
						"event-reader:%d", getpid()));
	new_thread(&tid, SCHED_FIFO, 1, writer, NULL);

	for (n = 0; n < TEST_COUNT; n++) {
		ret = read(evntfd, &val, sizeof(val));
		__Texpr_assert(ret == sizeof(val));
		__Texpr_assert(val == 1);
	}

	pthread_join(tid, NULL);

	return 0;
}
