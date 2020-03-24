/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <pthread.h>
#include <evl/thread.h>

int main(int argc, char *argv[])
{
	struct evl_thread_state statebuf;
	int tfd;

	tfd = evl_attach_self("test");
	evl_get_self();
	evl_switch_oob();
	evl_switch_inband();
	evl_get_state(tfd, &statebuf);

	return 0;
}
