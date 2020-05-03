/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <evl/thread.h>
#include <evl/sem.h>
#include <evl/xbuf.h>
#include <evl/proxy.h>
#include <evl/observable.h>
#include "helpers.h"

static inline bool element_is_public(const char *path)
{
	return access(path, F_OK) == 0;
}

int main(int argc, char *argv[])
{
	struct evl_sem sem;
	char *name, *path;
	int efd, ret;

	name = get_unique_name_and_path(EVL_THREAD_DEV, 0, &path); /* public */
	__Tcall_assert(efd, evl_attach_self(name));
	__Texpr_assert(element_is_public(path));
	__Tcall_assert(efd, evl_detach_self());

	__Tcall_assert(efd, evl_attach_self(name + 1)); /* private */
	__Texpr_assert(!element_is_public(path));
	__Tcall_assert(efd, evl_detach_self());

	__Tcall_assert(efd, evl_attach_self(NULL)); /* unnamed (private) */
	__Tcall_assert(efd, evl_detach_self());

	name = get_unique_name_and_path(EVL_MONITOR_DEV, 0, &path); /* public */
	__Tcall_assert(efd, evl_new_sem(&sem, name));
	__Texpr_assert(element_is_public(path));
	__Tcall_assert(efd, evl_close_sem(&sem));

	name = get_unique_name_and_path(EVL_MONITOR_DEV, 0, &path); /* private */
	__Tcall_assert(efd, evl_new_sem(&sem, name + 1));
	__Texpr_assert(!element_is_public(path));
	__Tcall_assert(efd, evl_close_sem(&sem));

	__Tcall_assert(efd, evl_new_sem(&sem, NULL)); /* unnamed (private) */
	__Tcall_assert(efd, evl_close_sem(&sem));

	name = get_unique_name_and_path(EVL_XBUF_DEV, 0, &path); /* public */
	__Tcall_assert(efd, evl_new_xbuf(1024, name));
	__Texpr_assert(element_is_public(path));
	__Tcall_assert(ret, close(efd));

	name = get_unique_name_and_path(EVL_XBUF_DEV, 0, &path); /* private */
	__Tcall_assert(efd, evl_new_xbuf(1024, name + 1));
	__Texpr_assert(!element_is_public(path));
	__Tcall_assert(ret, close(efd));

	__Tcall_assert(efd, evl_new_xbuf(1024, NULL)); /* unnamed (private) */
	__Tcall_assert(efd, close(efd));

	name = get_unique_name_and_path(EVL_PROXY_DEV, 0, &path); /* public */
	__Tcall_assert(efd, evl_new_proxy(1, 0, name));
	__Texpr_assert(element_is_public(path));
	__Tcall_assert(ret, close(efd));

	name = get_unique_name_and_path(EVL_PROXY_DEV, 0, &path); /* private */
	__Tcall_assert(efd, evl_new_proxy(1, 0, name + 1));
	__Texpr_assert(!element_is_public(path));
	__Tcall_assert(ret, close(efd));

	__Tcall_assert(efd, evl_new_proxy(1, 0, NULL)); /* unnamed (private) */
	__Tcall_assert(efd, close(efd));

	name = get_unique_name_and_path(EVL_OBSERVABLE_DEV, 0, &path); /* public */
	__Tcall_assert(efd, evl_new_observable(name));
	__Texpr_assert(element_is_public(path));
	__Tcall_assert(ret, close(efd));

	name = get_unique_name_and_path(EVL_PROXY_DEV, 0, &path); /* private */
	__Tcall_assert(efd, evl_new_observable(name + 1));
	__Texpr_assert(!element_is_public(path));
	__Tcall_assert(ret, close(efd));

	__Tcall_assert(efd, evl_new_observable(NULL)); /* unnamed (private) */
	__Tcall_assert(efd, close(efd));

	return 0;
}
