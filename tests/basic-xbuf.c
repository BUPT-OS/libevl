/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <evl/thread.h>
#include <evl/xbuf.h>
#include "helpers.h"

static void *peer(void *arg)
{
	const char *path = arg;
	int fd, n, nfd, nfd2;
	char buf[2];
	ssize_t ret;

	__Tcall_assert(fd, open(path, O_RDWR));
	__Tcall_assert(nfd, dup(fd));
	__Tcall_assert(nfd2, dup2(fd, nfd));

	for (n = 0; n < 3; n++) {
		__Tcall_errno_assert(ret, read(fd, buf, 2));
		if (ret != 2)
			break;
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	char *name, *path, buf[16];
	int tfd, xfd, n;
	pthread_t tid;
	ssize_t ret;

	__Tcall_assert(tfd, evl_attach_self("basic-xbuf:%d", getpid()));

	name = get_unique_name_and_path(EVL_XBUF_DEV, 0, &path);
	__Tcall_assert(xfd, evl_new_xbuf(1024, name));

	__Tcall_errno_assert(ret, write(xfd, "ABCD", 4));
	__Tcall_errno_assert(ret, write(xfd, "EF", 2));
	__Tcall_errno_assert(ret, write(xfd, "G", 1));
	__Tcall_errno_assert(ret, write(xfd, "H", 1));

	__Tcall_errno_assert(ret, fcntl(xfd, F_SETFL,
			fcntl(xfd, F_GETFL)|O_NONBLOCK));

	for (n = 0; n < 8; n++)
		__Tcall_errno_assert(ret, oob_read(xfd, buf, 1));

	new_thread(&tid, SCHED_OTHER, 0, peer, path);

	sleep(1);
	__Tcall_errno_assert(ret, oob_write(xfd, "01", 2));
	__Tcall_errno_assert(ret, oob_write(xfd, "23", 2));
	__Tcall_errno_assert(ret, oob_write(xfd, "45", 2));

	pthread_join(tid, NULL);

	return 0;
}
