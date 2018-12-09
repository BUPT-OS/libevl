/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evenless/thread.h>

int main(int argc, char *argv[])
{
	int efd, ret;

	efd = evl_attach_self("simple-bind:%d", getpid());
	printf("thread efd=%d\n", efd);

	ret = evl_detach_self();
	printf("detach ret=%d\n", ret);

	efd = evl_attach_self("simple-bind:%d", getpid());
	printf("thread efd=%d\n", efd);

	ret = evl_detach_self();
	printf("detach ret=%d\n", ret);

	return 0;
}
