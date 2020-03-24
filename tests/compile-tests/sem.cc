/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/sem.h>

static struct evl_sem sem1 =
	EVL_SEM_ANY_INITIALIZER("static-sem1", EVL_CLOCK_MONOTONIC, 0);

static struct evl_sem sem2 =
	EVL_SEM_INITIALIZER("static-sem2");

int main(int argc, char *argv[])
{
	struct timespec timeout;
	struct evl_sem dynsem;
	int val;

	evl_new_sem(&dynsem, "dynamic-sem");
	evl_new_sem_any(&dynsem, CLOCK_MONOTONIC, 0, "dynamic-sem");
	evl_open_sem(&dynsem, "dynamic-sem");
	evl_close_sem(&dynsem);
	evl_get_sem(&sem1);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedget_sem(&sem1, &timeout);
	evl_tryget_sem(&sem1);
	evl_peek_sem(&sem2, &val);
	evl_put_sem(&sem2);

	return 0;
}
