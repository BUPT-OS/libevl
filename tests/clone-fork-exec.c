/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	int efd;

	if (argc > 1 && strcmp(argv[1], "exec") == 0)
		return 0;

	__Tcall_assert(efd, evl_attach_self("clone-fork-exec:%d", getpid()));

	switch (fork()) {
	case 0:
		return 0;
	default:
		execlp(argv[0], "clone-fork-exec", "exec", NULL);
		return 1;
	}

	return 0;
}
