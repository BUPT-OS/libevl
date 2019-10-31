/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	int efd;

	__Tcall_assert(efd, evl_attach_self("simple-bind:%d", getpid()));

	return 0;
}
