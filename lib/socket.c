/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 *
 * An EVL socket is basically a regular socket which the EVL core
 * extends to support out-of-band communications.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <memory.h>
#include <evl/syscall.h>
#include <evl/net/socket.h>
#include "internal.h"

static const struct timespec zerotime;

ssize_t oob_recvmsg(int s, struct oob_msghdr *msghdr,
		const struct timespec *timeout,
		int flags)
{
	struct user_oob_msghdr u_msghdr;
	struct __evl_timespec kts;
	long ret;

	u_msghdr.iov_ptr = __evl_ptr64(msghdr->msg_iov);
	u_msghdr.iovlen = (__u32)msghdr->msg_iovlen;
	u_msghdr.ctl_ptr = __evl_ptr64(msghdr->msg_control);
	u_msghdr.ctllen = (__u32)msghdr->msg_controllen;
	u_msghdr.name_ptr = __evl_ptr64(msghdr->msg_name);
	u_msghdr.namelen = (__u32)msghdr->msg_namelen;
	u_msghdr.count = 0;
	u_msghdr.flags = flags;	/* in/out */
	u_msghdr.timeout = timeout ? *__evl_ktimespec(timeout, kts) :
		*__evl_ktimespec(&zerotime, kts);
	u_msghdr.timestamp = *__evl_ktimespec(&zerotime, kts);

	ret = oob_ioctl(s, EVL_SOCKIOC_RECVMSG, &u_msghdr);
	if (ret)
		return -errno;

	msghdr->msg_namelen = u_msghdr.namelen;
	msghdr->msg_controllen = u_msghdr.ctllen;
	msghdr->msg_flags = u_msghdr.flags;
	msghdr->msg_time.tv_sec = (time_t)u_msghdr.timestamp.tv_sec;
	msghdr->msg_time.tv_nsec = (long)u_msghdr.timestamp.tv_nsec;

	return (__ssize_t)u_msghdr.count;
}

ssize_t oob_sendmsg(int s, const struct oob_msghdr *msghdr,
		const struct timespec *timeout,
		int flags)
{
	struct user_oob_msghdr u_msghdr;
	struct __evl_timespec kts;
	long ret;

	u_msghdr.iov_ptr = __evl_ptr64(msghdr->msg_iov);
	u_msghdr.iovlen = (__u32)msghdr->msg_iovlen;
	u_msghdr.ctl_ptr = __evl_ptr64(msghdr->msg_control);
	u_msghdr.ctllen = (__u32)msghdr->msg_controllen;
	u_msghdr.name_ptr = __evl_ptr64(msghdr->msg_name);
	u_msghdr.namelen = (__u32)msghdr->msg_namelen;
	u_msghdr.count = 0;
	u_msghdr.flags = flags;	/* in */
	u_msghdr.timeout = timeout ? *__evl_ktimespec(timeout, kts) :
		*__evl_ktimespec(&zerotime, kts);
	u_msghdr.timestamp = *__evl_ktimespec(&msghdr->msg_time, kts);

	ret = oob_ioctl(s, EVL_SOCKIOC_SENDMSG, &u_msghdr);
	if (ret)
		return -errno;

	return (__ssize_t)u_msghdr.count;
}
