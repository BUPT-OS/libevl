/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/flags.h>

static struct evl_flags flags1 =
	EVL_FLAGS_ANY_INITIALIZER("static-flags1", EVL_CLOCK_MONOTONIC, 0);

static struct evl_flags flags2 =
	EVL_FLAGS_INITIALIZER("static-flags2");

int main(int argc, char *argv[])
{
	struct evl_flags dynflags;
	struct timespec timeout;
	int bits;

	evl_new_flags(&dynflags, "dynamic-flags");
	evl_new_flags_any(&dynflags, CLOCK_MONOTONIC, 0, "dynamic-flags");
	evl_open_flags(&dynflags, "dynamic-flags");
	evl_close_flags(&dynflags);
	evl_wait_flags(&flags1, &bits);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedwait_flags(&flags1, &timeout, &bits);
	evl_trywait_flags(&flags1, &bits);
	evl_peek_flags(&flags2, &bits);
	evl_post_flags(&flags2, bits);

	return 0;
}
