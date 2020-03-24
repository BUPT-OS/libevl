/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/tube.h>

DECLARE_EVL_CANISTER(long_canister, long);
static DECLARE_EVL_TUBE(tube_type, long_canister) tube;
static struct long_canister long_items[16];

static long build_test_tube(void)
{
	int count = sizeof(long_items) / sizeof(long_items[0]);
	long val;

	evl_init_tube(&tube, long_items, count);
	evl_send_tube(&tube, 0);
	evl_receive_tube(&tube, val);
	(void)val;

	return (size_t)evl_get_tube_size(tube_type, count);
}

DECLARE_EVL_CANISTER_REL(int_canister, int);
DECLARE_EVL_TUBE_REL(rel_tube_type, int_canister, int);

static int build_test_tube_rel(void)
{
	struct rel_tube_type *tube_rel;
	size_t size;
	void *mem;
	int val;

	size = evl_get_tube_size_rel(rel_tube_type, 100);
	mem = malloc(size);
	tube_rel = (struct rel_tube_type *)
		evl_init_tube_rel(rel_tube_type, int_canister, mem, size);
	evl_send_tube_rel(tube_rel, 0);
	evl_receive_tube_rel(tube_rel, val);

	return val;
}

int main(int argc, char *argv[])
{
	build_test_tube();
	build_test_tube_rel();

	return 0;
}
