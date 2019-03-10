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
	char buf[2];
	ssize_t ret;
	int fd, n, nfd;

	fd = open(path, O_RDWR);
	printf("dup(%d) => %d\n", fd, (nfd = dup(fd)));
	printf("dup2(%d, %d) => %d\n", fd, nfd, dup2(fd, nfd));

	printf("peer reading from fd=%d\n", fd);

	for (n = 0; n < 3; n++) {
		ret = read(fd, buf, 2);
		if (ret != 2)
			break;
		printf("inband[%d] => %.2s\n", n, buf);
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	char *name, *path, buf[16];
	int tfd, xfd, n;
	pthread_t tid;
	ssize_t ret;

	tfd = evl_attach_self("basic-xbuf:%d", getpid());
	printf("thread tfd=%d\n", tfd);

	name = get_unique_name_and_path("xbuf", 0, &path);
	__Tcall_assert(xfd, evl_new_xbuf(1024, 1024, name));

	printf("xfd=%d\n", xfd);

	ret = write(xfd, "ABCD", 4);
	printf("write->oob_read: %zd\n", ret);
	ret = write(xfd, "EF", 2);
	printf("write->oob_read: %zd\n", ret);
	ret = write(xfd, "G", 1);
	printf("write->oob_read: %zd\n", ret);
	ret = write(xfd, "H", 1);
	printf("write->oob_read: %zd\n", ret);

	__Tcall_errno_assert(ret, fcntl(xfd, F_SETFL,
			fcntl(xfd, F_GETFL)|O_NONBLOCK));

	for (n = 0; n < 8; n++) {
		__Tcall_assert(ret, oob_read(xfd, buf, 1));
		printf("oob_read[%d]<-write: %zd => %#x\n",
		       n, ret, *buf);
	}

	ret = new_thread(&tid, SCHED_OTHER, 0, peer, path);
	if (ret < 0)
		exit(1);

	sleep(1);
	ret = oob_write(xfd, "01", 2);
	printf("oob_write->read: %zd\n", ret);
	ret = oob_write(xfd, "23", 2);
	printf("oob_write->read: %zd\n", ret);
	ret = oob_write(xfd, "45", 2);
	printf("oob_write->read: %zd\n", ret);

	return pthread_join(tid, NULL);
}
