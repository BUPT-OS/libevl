/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_NET_SOCKET_H
#define _EVL_NET_SOCKET_H

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <uapi/evl/net/socket.h>

struct oob_msghdr {
	void		*msg_name;
	socklen_t	msg_namelen;
	struct iovec	*msg_iov;
	size_t		msg_iovlen;
	void		*msg_control;
	size_t		msg_controllen;
	int		msg_flags;
	struct timespec msg_time;
};

#ifdef __cplusplus
extern "C" {
#endif

ssize_t oob_recvmsg(int efd, struct oob_msghdr *msghdr,
		    const struct timespec *timeout,
		    int flags);

ssize_t oob_sendmsg(int efd, const struct oob_msghdr *msghdr,
		    const struct timespec *timeout,
		    int flags);

#ifdef __cplusplus
}
#endif

#endif /* _EVL_NET_SOCKET_H */
