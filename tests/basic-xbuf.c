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
#include <evl/rros.h>

// static void *peer(void *arg)
// {
// 	const int *xfd = arg;
// 	int fd, n, nfd, nfd2;
// 	char buf[2];
// 	ssize_t ret;

// 	DEBUG_PRINT("the xfd is %d\n", *xfd);
// 	fflush(stdout);
// 	// __Tcall_assert(fd, open(path, O_RDWR));
// 	fd = *xfd;
// 	DEBUG_PRINT("the fd is %d\n", fd);
// 	fflush(stdout);
// 	__Tcall_assert(nfd, dup(*xfd));
// 	DEBUG_PRINT("the nfd is %d\n", nfd);
// 	fflush(stdout);
// 	__Tcall_assert(nfd2, dup2(*xfd, nfd));
// 	DEBUG_PRINT("the nfd2 is %d\n", nfd2);
// 	fflush(stdout);

// 	for (n = 0; n < 3; n++) {
// 		__Tcall_errno_assert(ret, read(fd, buf, 2));
// 		if (ret != 2)
// 			break;
// 	}
// 	DEBUG_PRINT("read success\n");

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
	DEBUG_PRINT("attach basic-xbuf success\n");

	name = get_unique_name_and_path(EVL_XBUF_DEV, 0, &path);
	DEBUG_PRINT("path:%s\n", path);
	DEBUG_PRINT("name:%s\n", name);
	char *ch = "/X";
	char *new_name = (char*) malloc(strlen(name) + 3);
	strcpy(new_name, ch);
	strcat(new_name, name);
	new_name[0] = 'X';
	new_name[2] = 'h';
	DEBUG_PRINT("new_name is :%s\n", new_name);

	__Tcall_assert(xfd, evl_new_xbuf(1024, new_name));
	DEBUG_PRINT("the xfd = %d\n", xfd);
	DEBUG_PRINT("create new xbuf success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "ABCD", 4));
	DEBUG_PRINT("xbuf write ABCD success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "EF", 2));
	DEBUG_PRINT("xbuf write EF success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "G", 1));
	DEBUG_PRINT("xbuf write G success\n");
	fflush(stdout);
	__Tcall_errno_assert(ret, write(xfd, "H", 1));
	DEBUG_PRINT("xbuf write H success\n");
	fflush(stdout);

	DEBUG_PRINT("the xfd = %d\n", xfd);
	__Tcall_errno_assert(ret, fcntl(xfd, F_SETFL,
			fcntl(xfd, F_GETFL)|O_NONBLOCK));
	DEBUG_PRINT("the xfd = %d\n", xfd);
	DEBUG_PRINT("ret = %ld\n", ret);
	DEBUG_PRINT("xbuf fcntl success\n");
	fflush(stdout);

	for (n = 0; n < 8; n++)
	{
		__Tcall_errno_assert(ret, oob_read(xfd, buf, 1));
		DEBUG_PRINT("the value of oob read buf is %c\n", *buf);
	}
		
	DEBUG_PRINT("xbuf oob_read success\n");
	DEBUG_PRINT("xbuf write and oob_read success!!!!!\n");

	// nxfd = &xfd;
	// DEBUG_PRINT("nxfd:%d\n", *nxfd);
	// fflush(stdout);
	// new_thread(&tid, SCHED_OTHER, 0, peer, nxfd);
	// DEBUG_PRINT("xbuf new thread success\n");
	// fflush(stdout);

	// sleep(1);
	// __Tcall_errno_assert(ret, oob_write(xfd, "01", 2));
	// DEBUG_PRINT("xbuf oob_write 01 success\n");
	// fflush(stdout);
	// __Tcall_errno_assert(ret, oob_write(xfd, "23", 2));
	// DEBUG_PRINT("xbuf oob_write 23 success\n");
	// fflush(stdout);
	// __Tcall_errno_assert(ret, oob_write(xfd, "45", 2));
	// DEBUG_PRINT("xbuf oob_write 45 success\n");
	// fflush(stdout);

	// pthread_join(tid, NULL);

	free(new_name);
	return 0;
}
