/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/flags.h>

static struct evl_flags flags =
	EVL_FLAGS_INITIALIZER("static-flags", EVL_CLOCK_MONOTONIC, 0, 0);

int main(int argc, char *argv[])
{
	struct evl_flags dynflags;
	struct timespec timeout;
	int bits;

	evl_new_flags(&dynflags, "dynamic-flags");
	evl_create_flags(&dynflags, CLOCK_MONOTONIC, 0, 0, "dynamic-flags");
	evl_open_flags(&dynflags, "dynamic-flags");
	evl_close_flags(&dynflags);
	evl_wait_flags(&flags, &bits);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedwait_flags(&flags, &timeout, &bits);
	evl_trywait_flags(&flags, &bits);
	evl_peek_flags(&flags, &bits);
	evl_post_flags(&flags, bits);

	return 0;
}
