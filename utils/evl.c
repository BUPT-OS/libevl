/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <libgen.h>
#include <getopt.h>
#include <evl/evl.h>
#include <uapi/evl/control.h>

static char *find_install_dir(const char *arg0, const char *subdir)
{
	char *exepath, *bindir, *ipath;
	int ret;

	exepath = malloc(PATH_MAX);
	if (exepath == NULL)
		error(1, ENOMEM, "malloc");

	ret = readlink("/proc/self/exe", exepath, PATH_MAX);
	if (ret < 0)
		error(1, errno, "readlink");

	bindir = dirname(exepath);
	ret = asprintf(&ipath, "%s/%s", dirname(bindir), subdir);
	if (ret < 0)
		error(1, errno, "malloc");

	return ipath;
}

static void usage(void)
{
	fprintf(stderr, "usage: evl [options] [<command> [<args>]]\n");
        fprintf(stderr, "-P --prefix=<path>   set command path prefix\n");
        fprintf(stderr, "-V --version         print library and required ABI versions\n");
}

#define short_optlist "+P:V"

static const struct option options[] = {
	{
		.name = "prefix",
		.has_arg = required_argument,
		.val = 'P'
	},
	{
		.name = "version",
		.has_arg = no_argument,
		.val = 'V'
	},
	{ /* Sentinel */ }
};

int main(int argc, char *const argv[])
{
	char *cmddir = NULL, *cmdpath, **cmdargv, *cmd, *testdir;
	const char *arg0 = argv[0];
	int ret, c, n, cmdargc;

	opterr = 0;

	do {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		switch (c) {
		case 0:
		case 'P':
			cmddir = optarg;
			break;
		case 'V':
			printf("%s [requires ABI %d]\n",
				evl_get_version().version_string,
				EVL_ABI_PREREQ);
			exit(0);
		case '?':
			usage();
			return 2;
		default:
			break;
		}
	} while (c != EOF && c != '?');

	if (optind >= argc) {
		cmd = "help";
		optind = argc - 1;
	} else {
		if (c == '?') {
			usage();
			return 2;
		}
		cmd = argv[optind];
	}

	if (cmddir == NULL)
		cmddir = find_install_dir(arg0, "libexec");

	ret = asprintf(&cmdpath, "%s/evl-%s", cmddir, cmd);
	if (ret < 0)
		error(1, ENOMEM, "%s", arg0);

	testdir = find_install_dir(arg0, "tests");

	setenv("EVL_CMDDIR", cmddir, 1);
	setenv("EVL_TESTDIR", testdir, 1);
	setenv("EVL_SYSDIR", "/sys/devices/virtual", 1);
	setenv("EVL_TRACEDIR", "/sys/kernel/debug/tracing", 1);

	cmdargv = malloc(sizeof(char *) * (argc - optind + 1));
	if (cmdargv == NULL)
		error(1, ENOMEM, "malloc");
	cmdargv[0] = basename(cmdpath);
	for (n = optind + 1, cmdargc = 1; n < argc; n++)
		cmdargv[cmdargc++] = argv[n];
	cmdargv[cmdargc++] = NULL;

	execv(cmdpath, cmdargv);
	fprintf(stderr, "evl: undefined command '%s'\n", cmd);

	return 1;
}
