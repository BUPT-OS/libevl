/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/event.h>

static struct evl_event event =
	EVL_EVENT_INITIALIZER("static-event", EVL_CLOCK_MONOTONIC, 0);

int main(int argc, char *argv[])
{
	struct evl_event dynevent;
	struct timespec timeout;
	struct evl_mutex mutex;

	evl_new_mutex(&mutex, "mutex");
	evl_new_event(&dynevent, "dynamic-event");
	evl_create_event(&dynevent, CLOCK_MONOTONIC, 0, "dynamic-event");
	evl_open_event(&dynevent, "dynamic-event");
	evl_close_event(&dynevent);
	evl_wait_event(&event, &mutex);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedwait_event(&event, &mutex, &timeout);
	evl_signal_event(&event);
	evl_broadcast_event(&event);
	evl_signal_thread(&event, -1);

	return 0;
}
