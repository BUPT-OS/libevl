/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/sem.h>

static struct evl_sem sem =
	EVL_SEM_INITIALIZER("static-sem", EVL_CLOCK_MONOTONIC, 0, 0);

int main(int argc, char *argv[])
{
	struct timespec timeout;
	struct evl_sem dynsem;
	int val;

	evl_new_sem(&dynsem, "dynamic-sem");
	evl_create_sem(&dynsem, CLOCK_MONOTONIC, 0, 0, "dynamic-sem");
	evl_open_sem(&dynsem, "dynamic-sem");
	evl_close_sem(&dynsem);
	evl_get_sem(&sem);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedget_sem(&sem, &timeout);
	evl_tryget_sem(&sem);
	evl_peek_sem(&sem, &val);
	evl_put_sem(&sem);

	return 0;
}
