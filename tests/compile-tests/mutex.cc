/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/mutex.h>

static struct evl_mutex mutex =
	EVL_MUTEX_INITIALIZER("static-mutex",
			      EVL_CLOCK_MONOTONIC,
			      0, EVL_MUTEX_NORMAL);

int main(int argc, char *argv[])
{
	struct evl_mutex dynmutex;
	struct timespec timeout;

	evl_new_mutex(&dynmutex, "dynamic-mutex");
	evl_create_mutex(&dynmutex, CLOCK_MONOTONIC, 0,
			  EVL_MUTEX_NORMAL, "dynamic-mutex");
	evl_open_mutex(&dynmutex, "dynamic-mutex");
	evl_close_mutex(&dynmutex);
	evl_lock_mutex(&dynmutex);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedlock_mutex(&dynmutex, &timeout);
	evl_trylock_mutex(&mutex);
	evl_unlock_mutex(&mutex);
	evl_set_mutex_ceiling(&mutex, 0);
	evl_get_mutex_ceiling(&mutex);

	return 0;
}
