/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <unistd.h>
#include <search.h>
#include <pthread.h>
#include <evl/evl.h>
#include <evl/thread.h>
#include <sys/eventfd.h>
#include "internal.h"

static __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
int evl_efd = -1;

static __thread __attribute__ ((tls_model (EVL_TLS_MODEL)))
struct evl_tsd {
	int fd;
	pthread_t thread;
} evl_tsd;

static pthread_key_t tsd_key;

static void *fdtree_root;

static int compare_nodes(const void *l, const void *r)
{
	const struct evl_tsd *lt = l, *rt = r;

	return lt->fd - rt->fd;
}

int evl_attach_self(const char *fmt, ...)
{
	struct evl_tsd **node;
	int ret, fd;

	if (evl_efd != -1)
		return -EBUSY;

	ret = evl_init();
	if (ret)
		return ret;

	fd = eventfd(0, EFD_CLOEXEC);
	if (fd < 0)
		return -errno;

	evl_tsd.fd = fd;
	evl_tsd.thread = pthread_self();
	node = (struct evl_tsd **)tsearch(&evl_tsd, &fdtree_root, compare_nodes);
	if (!node) {
		close(fd);
		return -ENOMEM;
	}
	if ((*node)->fd != fd)
		return -EBUSY;	/* Weird. */

	pthread_setspecific(tsd_key, &evl_tsd);
	evl_efd = fd;

	return fd;
}

int evl_detach_self(void)
{
	if (evl_efd < 0)
		return -EPERM;

	pthread_setspecific(tsd_key, NULL);
	tdelete(&evl_tsd, &fdtree_root, compare_nodes);
	close(evl_efd);
	evl_efd = -1;

	return 0;
}

int evl_get_self(void)
{
	return evl_efd;
}

pthread_t eshi_find_thread_by_fd(int fd)
{
	struct evl_tsd tsd = {
		.fd = fd,
	}, **node;

	node = tsearch(&tsd, &fdtree_root, compare_nodes);
	if (!node)
		return (pthread_t)NULL;

	return (*node)->thread;
}

static void unregister_thread(void *p)
{
	evl_detach_self();
}

int eshi_init_threads(void)
{
	return -pthread_key_create(&tsd_key, unregister_thread);
}
