/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <evenless/proxy.h>
#include <evenless/thread.h>

int main(int argc, char *argv[])
{
	int ret, efd, logfd;

	efd = evl_attach_self("simple-bind:%d", getpid());
	printf("efd=%d\n", efd);
	logfd = evl_new_proxy(fileno(stdout), 16, "stdout:%d", getpid());
	printf("logfd=%d\n", logfd);
	ret = oob_write(logfd, "stdout relay!\n", 14);
	printf("oob_write=%d, errno=%d\n", ret, errno);

	return 0;
}
