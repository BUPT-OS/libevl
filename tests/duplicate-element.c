/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evenless/clock.h>
#include <evenless/sem.h>
#include <evenless/evl.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	struct evl_sem sem;
	int ret, sfd;
	char *name;

	__Tcall_assert(ret, evl_init());

	name = get_unique_name("monitor", 0);
	__Tcall_assert(sfd, evl_new_sem(&sem, EVL_CLOCK_MONOTONIC, 0, name));

	name = get_unique_name("monitor", 0);
	__Fcall_assert(sfd, evl_new_sem(&sem, EVL_CLOCK_MONOTONIC, 0, name));
	__Texpr_assert(sfd == -EEXIST);

	return 0;
}
