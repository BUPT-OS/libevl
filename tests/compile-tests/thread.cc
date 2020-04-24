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
	tfd = evl_attach_thread(EVL_CLONE_PUBLIC, "test");
	evl_detach_self();
	evl_detach_thread(0);
	evl_get_self();
	evl_switch_oob();
	evl_switch_inband();
	evl_get_state(tfd, &statebuf);
	evl_unblock_thread(tfd);
	evl_demote_thread(tfd);
	evl_set_thread_mode(tfd, 0, NULL);
	evl_clear_thread_mode(tfd, 0, NULL);
	evl_subscribe(tfd, 1024, 0);
	evl_unsubscribe(tfd);

	return 0;
}
