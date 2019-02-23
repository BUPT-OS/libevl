/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <evenless/thread.h>
#include <evenless/xbuf.h>
#include <evenless/clock.h>
#include <evenless/poll.h>
#include "helpers.h"

static const char *msg[] = {
	"1",
	"7",
	"3",
	"8",
	"5",
	"010",
	"232",
	"137",
	"852",
	"699",
	"335",
	"118",
	"764836",
	"274520",
	"453098",
	"129987",
	"875342",
	"336491",
	NULL,
};

static void *writer_thread(void *arg)
{
	const char *path = arg;
	int fd, n = 0;
	ssize_t len;

	fd = open(path, O_RDWR);

	while (msg[n]) {
		len = strlen(msg[n]);
		__Texpr_assert(write(fd, msg[n], len) == len);
		n++;
		usleep(10000);
	}

	close(fd);

	return NULL;
}

int main(int argc, char *argv[])
{
	struct evl_poll_event pollset;
	char *name, *path, buf[16];
	struct sched_param param;
	int tfd, xfd, pfd, n;
	pthread_t writer;
	ssize_t ret;

	param.sched_priority = 1;
	__Texpr_assert(pthread_setschedparam(pthread_self(),
				SCHED_FIFO, &param) == 0);
	__Tcall_assert(tfd, evl_attach_self("poller-read:%d", getpid()));

	name = get_unique_name_and_path("xbuf", 0, &path);
	__Tcall_assert(xfd, evl_new_xbuf(1024, 1024, name));

	/*
	 * We want to read any data present in the cross buffer,
	 * whatever the length. Switching to O_NONBLOCK allows short
	 * reads.
	 */
	__Tcall_errno_assert(ret, fcntl(xfd, F_SETFL,
			fcntl(xfd, F_GETFL)|O_NONBLOCK));

	ret = new_thread(&writer, SCHED_OTHER, 0, writer_thread, path);
	if (ret < 0)
		exit(1);

	__Tcall_assert(pfd, evl_new_poll());
	__Tcall_assert(ret, evl_add_pollfd(pfd, xfd, POLLIN));

	for (n = 0; n < sizeof(msg) / sizeof(msg[0]) - 1; n++) {
		__Tcall_assert(ret, evl_poll(pfd, &pollset, 1));
		__Texpr_assert(ret == 1);
		__Texpr_assert(pollset.events == POLLIN);
		__Texpr_assert(pollset.fd == xfd);
		ret = oob_read(xfd, buf, sizeof(buf) - 1);
		__Texpr_assert(ret > 0 && ret < sizeof(buf));
		buf[ret] = '\0';
		__Texpr_assert(strncmp(msg[n], buf, strlen(msg[n])) == 0);
	}

	__Texpr_assert(pthread_join(writer, NULL) == 0);

	close(pfd);
	close(xfd);
	close(tfd);

	return 0;
}
