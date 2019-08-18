/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <evl/clock.h>
#include <evl/sem.h>
#include <evl/evl.h>
#include "helpers.h"

int main(int argc, char *argv[])
{
	struct evl_sem sem;
	int ret, sfd;
	char *name;

	__Tcall_assert(ret, evl_init());

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Tcall_assert(sfd, evl_new_sem(&sem, name));

	name = get_unique_name(EVL_MONITOR_DEV, 0);
	__Fcall_assert(sfd, evl_new_sem(&sem, name));
	__Texpr_assert(sfd == -EEXIST);

	return 0;
}
