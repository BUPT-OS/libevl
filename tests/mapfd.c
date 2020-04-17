/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <evl/proxy.h>
#include "helpers.h"

static inline int do_memfd_create(const char *name, int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

int main(int argc, char *argv[])
{
	int memfd, efd, ret;
	char *name;
	void *p;

	if (argc > 1) {
		ret = chdir("/dev/evl/proxy");
		(void)ret;
		efd = open(argv[1], O_RDWR);
		if (efd < 0)
			error(1, errno, "%s open %s", argv[0], argv[1]);
		p = mmap(NULL, 1024, PROT_READ|PROT_WRITE,
			 MAP_SHARED, efd, 0);
		if (p == MAP_FAILED)
			error(1, errno, "mmap child");
		return 0;
	}

	memfd = do_memfd_create("evl", 0);
	if (memfd < 0)
		error(1, errno, "memfd_create");

	ret = ftruncate(memfd, 1024);
	if (ret)
		error(1, errno, "ftruncate");

	p = mmap(NULL, 1024, PROT_READ|PROT_WRITE,
		 MAP_SHARED, memfd, 0);
	if (p == MAP_FAILED)
		error(1, errno, "mmap");

	strcpy(p, "mapfd-test");

	ret = asprintf(&name, "/proxy:%d", getpid());
	(void)ret;
	__Tcall_assert(efd, evl_new_proxy(memfd, 0, "%s", name));

	switch (fork()) {
	case 0:
		sleep(1);
		return 0;
	default:
		execlp(argv[0], "mapfd", name + 1, NULL);
		return 1;
	}

	return 0;
}
