/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/observable.h>

int main(int argc, char *argv[])
{
	struct evl_notification nf;
	struct evl_notice no;
	int efd;

	evl_new_observable("test");
	efd = evl_create_observable(EVL_CLONE_PRIVATE, "test");
	evl_update_observable(efd, &no, 1);
	evl_read_observable(efd, &nf, 1);

	return 0;
}
