/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/clock.h>
#include <evl/event.h>

static struct evl_event event1 =
	EVL_EVENT_ANY_INITIALIZER("static-event1", EVL_CLOCK_MONOTONIC);

static struct evl_event event2 =
	EVL_EVENT_INITIALIZER("static-event2");

int main(int argc, char *argv[])
{
	struct evl_event dynevent;
	struct timespec timeout;
	struct evl_mutex mutex;

	evl_new_mutex(&mutex, "mutex");
	evl_new_event(&dynevent, "dynamic-event");
	evl_new_event_any(&dynevent, CLOCK_MONOTONIC, "dynamic-event");
	evl_open_event(&dynevent, "dynamic-event");
	evl_close_event(&dynevent);
	evl_wait_event(&event1, &mutex);
	evl_read_clock(EVL_CLOCK_MONOTONIC, &timeout);
	evl_timedwait_event(&event1, &mutex, &timeout);
	evl_signal_event(&event1);
	evl_broadcast_event(&event2);
	evl_signal_thread(&event1, -1);

	return 0;
}
