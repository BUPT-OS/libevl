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

static char *find_command_dir(const char *arg0)
{
	char *cmddir;
	int ret;

	cmddir = malloc(PATH_MAX);
	if (cmddir == NULL)
		error(1, ENOMEM, "malloc");

	ret = readlink("/proc/self/exe", cmddir, PATH_MAX);
	if (ret < 0)
		error(1, errno, "readlink");

	return dirname(cmddir);
}

static void usage(void)
{
	fprintf(stderr, "usage: evl [options] <command> [<args>]\n");
        fprintf(stderr, "-P --prefix=<path>   set command path prefix\n");
}

#define short_optlist "P:"

static const struct option options[] = {
	{
		.name = "prefix",
		.has_arg = required_argument,
		.val = 'P'
	},
	{ /* Sentinel */ }
};

static int is_word(const char *s)
{
	while (*s && isalpha(*s))
		s++;

	return *s == '\0';
}
	
int main(int argc, char *const argv[])
{
	char *cmddir = NULL, *cmdpath, **cmdargv, *cmd;
	int ret, c, n, wcount, wpos = -1, cmdargc;
	const char *arg0 = argv[0];

	if (argc < 2) {
		wpos = 0;
		cmd = strdup("help");
		goto run;
	}

	for (n = 1, wcount = 0; n < argc; n++) {
		if (is_word(argv[n])) {
			wpos = n;
			if (++wcount > 1)
				break;
		} else
			wcount = 0;
	}

	if (wpos < 0) {
		fprintf(stderr, "evl: no command word\n");
		usage();
		return 2;
	}

	cmd = strdup(argv[wpos]);
	*argv[wpos] = '\0';

	opterr = 0;

	for (;;) {
		c = getopt_long(wpos, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 'P':
			cmddir = optarg;
			break;
		case '?':
		default:
			usage();
			return 2;
		}
	}
run:
	if (cmddir == NULL)
		cmddir = find_command_dir(arg0);

	ret = asprintf(&cmdpath, "%s/evl-%s", cmddir, cmd);
	if (ret < 0)
		error(1, ENOMEM, arg0);

	setenv("EVL_CMDDIR", cmddir, 1);
	setenv("EVL_SYSDIR", "/sys/devices/virtual", 1);
	setenv("EVL_TRACEDIR", "/sys/kernel/debug/tracing", 1);

	cmdargv = malloc(sizeof(char *) * (argc - wpos + 1));
	if (cmdargv == NULL)
		error(1, ENOMEM, "malloc");
	cmdargv[0] = basename(cmdpath);
	for (cmdargc = 1, n = wpos + 1; n < argc; n++)
		cmdargv[cmdargc++] = argv[n];
	cmdargv[cmdargc] = NULL;

	execv(cmdpath, cmdargv);
	fprintf(stderr, "evl: undefined command '%s'\n", cmd);

	return 1;
}
