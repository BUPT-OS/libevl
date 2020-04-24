/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/observable.h>

int main(int argc, char *argv[])
{
	evl_new_observable("test");
	evl_create_observable(EVL_CLONE_PRIVATE, "test");

	return 0;
}
