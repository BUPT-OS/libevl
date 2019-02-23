/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>
#include <uapi/evenless/factory.h>
#include "internal.h"

/*
 * Creating a Evenless element is done by the following steps:
 *
 * 1. open the clone device of the proper element class.
 *
 * 2. issue ioctl(EVL_IOC_CLONE) to create a new element, passing
 * an attribute structure.
 *
 * 3. open the new element device, which file descriptor is returned
 * to the caller.
 *
 * Except for threads, closing the last file descriptor referring to
 * an element causes its automatic deletion.
 */
int create_evl_element(const char *type, const char *name,
		       void *attrs, struct evl_element_ids *eids)
{
	struct evl_clone_req clone;
	char *fdevname, *edevname;
	int ffd, efd, ret;

	ret = asprintf(&fdevname, "/dev/evenless/%s/clone", type);
	if (ret < 0)
		return -ENOMEM;

	ffd = open(fdevname, O_RDWR);
	if (ffd < 0) {
		ret = -errno;
		goto out_factory;
	}

	clone.name = name;
	clone.attrs = attrs;
	ret = ioctl(ffd, EVL_IOC_CLONE, &clone);
	if (ret) {
		ret = -errno;
		goto out_new;
	}

	if (name)
		ret = asprintf(&edevname, "/dev/evenless/%s/%s",
			       type, name);
	else
		ret = asprintf(&edevname, "/dev/evenless/%s/%d",
			       type, clone.eids.minor);
	if (ret < 0) {
		ret = -ENOMEM;
		goto out_new;
	}

	efd = open(edevname, O_RDWR);
	if (efd < 0) {
		ret = -errno;
		goto out_element;
	}

	ret = fcntl(efd, F_GETFD, 0);
	if (ret < 0) {
		ret = -errno;
		goto out_element;
	}

	ret = fcntl(efd, F_SETFD, ret | O_CLOEXEC);
	if (ret) {
		ret = -errno;
		goto out_element;
	}

	if (eids)
		*eids = clone.eids;

	ret = efd;

out_element:
	free(edevname);
out_new:
	close(ffd);
out_factory:
	free(fdevname);

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

	ret = asprintf(&path, "/dev/evenless/%s/%s", type, name);
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
	int fd, ret;

	ret = asprintf(&devname, "/dev/evenless/%s", type);
	if (ret < 0)
		return -ENOMEM;

	fd = open(devname, O_RDWR);
	if (fd < 0) {
		ret = -errno;
		goto fail;
	}

	ret = fcntl(fd, F_GETFD, 0);
	if (ret < 0) {
		ret = -errno;
		goto fail_setfd;
	}

	ret = fcntl(fd, F_SETFD, ret | O_CLOEXEC);
	if (ret) {
		ret = -errno;
		goto fail_setfd;
	}

	free(devname);

	return fd;

fail_setfd:
	close(fd);
fail:
	free(devname);

	return ret;
}
