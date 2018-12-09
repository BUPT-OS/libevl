/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evenless/thread.h>

int main(int argc, char *argv[])
{
	int efd;

	if (argc > 1 && strcmp(argv[1], "exec") == 0) {
		printf("exec() ok for pid %d\n", getpid());
		return 0;
	}

	efd = evl_attach_self("clone-fork-exec:%d", getpid());
	printf("thread has efd=%d\n", efd);

	switch (fork()) {
	case 0:
		return 0;
	default:
		execlp(argv[0], "clone-fork-exec", "exec", NULL);
		printf("exec() failed for pid %d\n", getpid());
	}

	return 0;
}
