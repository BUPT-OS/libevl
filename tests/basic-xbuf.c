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

// static void *peer(void *arg)
// {
// 	const int *xfd = arg;
// 	int fd, n, nfd, nfd2;
// 	char buf[2];
// 	ssize_t ret;

// 	printf("the xfd is %d\n", *xfd);
// 	fflush(stdout);
// 	// __Tcall_assert(fd, open(path, O_RDWR));
// 	fd = *xfd;
// 	printf("the fd is %d\n", fd);
// 	fflush(stdout);
// 	__Tcall_assert(nfd, dup(*xfd));
// 	printf("the nfd is %d\n", nfd);
// 	fflush(stdout);
// 	__Tcall_assert(nfd2, dup2(*xfd, nfd));
// 	printf("the nfd2 is %d\n", nfd2);
// 	fflush(stdout);

// 	for (n = 0; n < 3; n++) {
// 		__Tcall_errno_assert(ret, read(fd, buf, 2));
// 		if (ret != 2)
// 			break;
// 	}
// 	printf("read success\n");

// 	return NULL;
// }

int main(int argc, char *argv[])
{
	char *name, *path, buf[16];
	int tfd, xfd, n;
	// int *nxfd;
	// pthread_t tid;
	ssize_t ret;

	__Tcall_assert(tfd, evl_attach_self("basic-xbuf:%d", getpid()));
	fflush(stdout);
	printf("attach basic-xbuf success\n");

	name = get_unique_name_and_path(EVL_XBUF_DEV, 0, &path);
	printf("path:%s\n", path);
	printf("name:%s\n", name);
	char *ch = "/X";
	char *new_name = (char*) malloc(strlen(name) + 3);
	strcpy(new_name, ch);
	strcat(new_name, name);
	new_name[0] = 'X';
	new_name[2] = 'h';
	printf("new_name is :%s\n", new_name);

	__Tcall_assert(xfd, evl_new_xbuf(1024, new_name));
	printf("the xfd = %d\n", xfd);
	printf("create new xbuf success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "ABCD", 4));
	printf("xbuf write ABCD success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "EF", 2));
	printf("xbuf write EF success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "G", 1));
	printf("xbuf write G success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "H", 1));
	printf("xbuf write H success\n");
	fflush(stdout);

	printf("the xfd = %d\n", xfd);
	__Tcall_errno_assert(ret, fcntl(xfd, F_SETFL,
			fcntl(xfd, F_GETFL)|O_NONBLOCK));
	printf("the xfd = %d\n", xfd);
	printf("ret = %ld\n", ret);
	printf("xbuf fcntl success\n");
	fflush(stdout);

	for (n = 0; n < 8; n++)
	{
		__Tcall_errno_assert(ret, oob_read(xfd, buf, 1));
		printf("the value of oob read buf is %c\n", *buf);
	}
		
	printf("xbuf oob_read success\n");
	printf("xbuf write and oob_read success!!!!!\n");

	// nxfd = &xfd;
	// printf("nxfd:%d\n", *nxfd);
	// fflush(stdout);
	// new_thread(&tid, SCHED_OTHER, 0, peer, nxfd);
	// printf("xbuf new thread success\n");
	// fflush(stdout);

	// sleep(1);
	// __Tcall_errno_assert(ret, oob_write(xfd, "01", 2));
	// printf("xbuf oob_write 01 success\n");
	// fflush(stdout);
	// __Tcall_errno_assert(ret, oob_write(xfd, "23", 2));
	// printf("xbuf oob_write 23 success\n");
	// fflush(stdout);
	// __Tcall_errno_assert(ret, oob_write(xfd, "45", 2));
	// printf("xbuf oob_write 45 success\n");
	// fflush(stdout);

	// pthread_join(tid, NULL);

	free(new_name);
	return 0;
}
