/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <linux/types.h>
#include <uapi/evl/factory.h>
#include "internal.h"

static void do_lart_once(void)
{
	fprintf(stderr,	"evl: core present but stopped\n");
}

static void lart_once(void)
{
	static pthread_once_t lart_once = PTHREAD_ONCE_INIT;
	pthread_once(&lart_once, do_lart_once);
}

static int flip_fd_flags(int efd, int cmd, int flags)
{
	int ret;

	printf("clone flags %d\n", 1);
	ret = fcntl(efd, cmd == F_SETFD ? F_GETFD : F_GETFL, 0);

	printf("clone flags %d\n", 2);
	if (ret < 0)
		return -errno;

	printf("clone flags %d\n", 4);
	ret = fcntl(efd, cmd, ret | flags);

	printf("clone flags %d\n", 3);
	if (ret)
		return -errno;

	return 0;
}

/*
 * Creating an EVL element is done in the following steps:
 *
 * 1. open the clone device of the proper element class.
 *
 * 2. issue ioctl(EVL_IOC_CLONE) to create a new element, passing
 * an attribute structure.
 *
 * 3. if EVL_CLONE_PUBLIC was mentioned in the clone_flags, open the
 * new element device to get a file descriptor on it; otherwise an
 * open descriptor is returned by EVL_IOC_CLONE for private
 * elements.
 *
 * Except for threads, closing the last file descriptor referring to
 * an element causes its automatic deletion.
 */
int create_evl_element(const char *type, const char *name,
		void *attrs, int clone_flags,
		struct evl_element_ids *eids)
{
	char *fdevname, *edevname = NULL;
	struct evl_clone_req clone;
	int ffd, efd, ret;
	bool nonblock;

	nonblock = !!(clone_flags & EVL_CLONE_NONBLOCK);
	/* Strip off user-only bits. */
	clone_flags &= EVL_CLONE_MASK;
	clone_flags &= ~EVL_CLONE_NONBLOCK;

	ret = asprintf(&fdevname, "/dev/evl/%s/clone", type);
	if (ret < 0)
		return -ENOMEM;

	printf("fdevname: %s\n", fdevname);
	ffd = open(fdevname, O_RDWR);
	if (ffd < 0) {
		printf("open fdevname failed, the ffd is %d\n", ffd);
		ret = -errno;
		goto out_factory;
	}

	printf("ffd = %d\n",ffd);
	printf("fdevname: %s\n", fdevname);

	/*
	 * Turn on public mode if the user-provided name starts with a
	 * slash.  Anonymous elements must be private by definition.
	 */
	if (name == NULL) {
		if (clone_flags & EVL_CLONE_PUBLIC)
			return -EINVAL;
	} else if (*name == '/') {
		clone_flags |= EVL_CLONE_PUBLIC;
		name++;
	}

	printf("fdevname: %s\n", fdevname);
	clone.name_ptr = __evl_ptr64(name);
	clone.attrs_ptr = __evl_ptr64(attrs);
	clone.clone_flags = clone_flags;
	ret = ioctl(ffd, EVL_IOC_CLONE, &clone);
	if (ret) {
		printf("ffd failed, ret=%d\n", ret);
		fflush(stdout);
		ret = -errno;
		if (ret == -ENXIO)
			lart_once();
		goto out_new;
	}
	printf("clone flags %d\n", clone_flags);
	if (clone_flags & EVL_CLONE_PUBLIC) {
		ret = asprintf(&edevname, "/dev/evl/%s/%s", type, name);
		if (ret < 0) {
			ret = -ENOMEM;
			goto out_new;
		}
		printf("edevname: %s\n", edevname);
		efd = open(edevname, O_RDWR);
		if (efd < 0) {
			printf("edevname: %s failed\n", edevname);
			ret = -errno;
			goto out_element;
		}
	} else {
		efd = clone.efd;
	}
	printf("efd is in the user space %d %d \n ", clone.efd, efd);
	printf("clone flags %d\n", clone_flags);
	ret = flip_fd_flags(efd, F_SETFD, O_CLOEXEC);

	printf("clone flags %d\n", clone_flags);
	printf("flip_fd_flags, ret=%d\n", ret);
	fflush(stdout);
	if (ret)
		goto out_element;

	printf("flip_fd_flags, ret=%d\n", ret);
	if (nonblock) {
		ret = flip_fd_flags(efd, F_SETFL, O_NONBLOCK);
		printf("flip_fd_flags, ret=%d\n", ret);
		if (ret)
			goto out_element;
	}

	printf("flip_fd_flags, ret=%d\n", ret);
	printf("the filp of fdevname is %p, the fdevname is %s\n", fdevname, fdevname);
	fflush(stdout);

	if (eids) {
		printf("the filp of eids is %p\n", eids);
		*eids = clone.eids;
	}

	ret = efd;

out_element:
	if (edevname)
		free(edevname);
out_new:
	printf("before close ffd\n");
	printf("ffd = %d\n",ffd);
	fflush(stdout);
	close(ffd);
	printf("after close ffd\n");
	fflush(stdout);
out_factory:
	printf("before free fdevname\n");
	fflush(stdout);
	free(fdevname);
	printf("free fdevname success\n");
	fflush(stdout);

	return ret;
}

int open_evl_element_vargs(const char *type,
		const char *fmt, va_list ap)
{
	char *path, *name;
	int efd, ret;

	ret = vasprintf(&name, fmt, ap);
	if (ret < 0)
		return -ENOMEM;

	ret = asprintf(&path, "/dev/evl/%s/%s", type, name);
	free(name);
	if (ret < 0)
		return -ENOMEM;

	efd = open(path, O_RDWR);
	if (efd < 0) {
		ret = -errno;
		goto fail_open;
	}

	ret = fcntl(efd, F_GETFD, 0);
	if (ret < 0) {
		ret = -errno;
		goto fail;
	}

	ret = fcntl(efd, F_SETFD, ret | O_CLOEXEC);
	if (ret) {
		ret = -errno;
		goto fail;
	}

	free(path);

	return efd;

fail:
	close(efd);
fail_open:
	free(path);

	return ret;
}

int open_evl_element(const char *type, const char *fmt, ...)
{
	va_list ap;
	int efd;

	va_start(ap, fmt);
	efd = open_evl_element_vargs(type, fmt, ap);
	va_end(ap);

	return efd;
}

int create_evl_file(const char *type)
{
	char *devname;
	int efd, ret;

	ret = asprintf(&devname, "/dev/evl/%s", type);
	if (ret < 0)
		return -ENOMEM;

	efd = open(devname, O_RDWR);
	if (efd < 0) {
		ret = -errno;
		goto fail;
	}

	ret = flip_fd_flags(efd, F_SETFD, O_CLOEXEC);
	if (ret)
		goto fail_setfd;

	free(devname);

	return efd;

fail_setfd:
	close(efd);
fail:
	free(devname);

	return ret;
}
