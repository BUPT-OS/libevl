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
	int efd, ret;

	__Tcall_assert(efd, evl_attach_self("simple-bind:%d", getpid()));
	__Tcall_assert(ret, evl_detach_self());
	__Tcall_assert(efd, evl_attach_self("simple-bind:%d", getpid()));
	__Tcall_assert(ret, evl_detach_self());

	return 0;
}
