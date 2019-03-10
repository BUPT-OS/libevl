/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>

int main(int argc, char *argv[])
{
	int efd;

	efd = evl_attach_self("simple-bind:%d", getpid());
	printf("thread efd=%d\n", efd);

	return 0;
}
