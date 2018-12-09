/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <evenless/thread.h>
#include <evenless/xbuf.h>
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

	name = get_unique_name("xbuf", 0, &path);
	xfd = evl_new_xbuf(1024, 1024, name);
	if (xfd < 0)
		error(1, -xfd, "evl_new_xbuf");

	printf("xfd=%d\n", xfd);

	ret = write(xfd, "ABCD", 4);
	printf("write->oob_read: %zd\n", ret);
	ret = write(xfd, "EF", 2);
	printf("write->oob_read: %zd\n", ret);
	ret = write(xfd, "G", 1);
	printf("write->oob_read: %zd\n", ret);
	ret = write(xfd, "H", 1);
	printf("write->oob_read: %zd\n", ret);

	ret = fcntl(xfd, F_SETFL, fcntl(xfd, F_GETFL)|O_NONBLOCK);
	if (ret)
		error(1, errno, "oob_read");

	for (n = 0; n < 8; n++) {
		ret = oob_read(xfd, buf, 1);
		if (ret < 0)
			error(1, errno, "oob_read");
		printf("oob_read[%d]<-write: %zd => %#x\n",
		       n, ret, *buf);
	}

	ret = pthread_create(&tid, NULL, peer, path);
	sleep(1);

	ret = oob_write(xfd, "01", 2);
	printf("oob_write->read: %zd\n", ret);
	ret = oob_write(xfd, "23", 2);
	printf("oob_write->read: %zd\n", ret);
	ret = oob_write(xfd, "45", 2);
	printf("oob_write->read: %zd\n", ret);
	
	return pthread_join(tid, NULL);
}
