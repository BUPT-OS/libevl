/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/evl.h>

int main(int argc, char *argv[])
{
	struct evl_version v;

	evl_init();
	v = evl_get_version();

	return v.api_level;
}
