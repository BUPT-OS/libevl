/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/mutex.h>

static struct evl_mutex mutex1 =
	EVL_MUTEX_ANY_INITIALIZER(EVL_MUTEX_NORMAL,
				"static-mutex1",
				EVL_CLOCK_MONOTONIC, 0);

static struct evl_mutex mutex2 =
	EVL_MUTEX_INITIALIZER("static-mutex2");

int main(int argc, char *argv[])
{
	struct evl_mutex dynmutex;
	struct timespec timeout;

	evl_new_mutex(&dynmutex, "dynamic-mutex");
	evl_new_mutex_any(&dynmutex, EVL_MUTEX_NORMAL,
			CLOCK_MONOTONIC, 0, "dynamic-mutex");
	evl_open_mutex(&dynmutex, "dynamic-mutex");
	evl_close_mutex(&dynmutex);
	evl_lock_mutex(&dynmutex);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedlock_mutex(&dynmutex, &timeout);
	evl_trylock_mutex(&mutex1);
	evl_unlock_mutex(&mutex1);
	evl_set_mutex_ceiling(&mutex2, 0);
	evl_get_mutex_ceiling(&mutex2);

	return 0;
}
