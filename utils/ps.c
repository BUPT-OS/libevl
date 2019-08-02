/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <unistd.h>
#include <stdint.h>
#include <getopt.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <libgen.h>
#include <memory.h>
#include <search.h>
#include <limits.h>
#include <error.h>
#include <errno.h>
#include <ftw.h>
#include <uapi/evl/control.h>
#include <uapi/evl/thread.h>

static cpu_set_t cpu_restrict;

#define DISPLAY_STATE  1
#define DISPLAY_TIMES  2
#define DISPLAY_SCHED  4

#define DISPLAY_NUMERIC    32
#define DISPLAY_MODIFIERS  DISPLAY_NUMERIC

#define DISPLAY_DEFAULT_FORMAT  DISPLAY_SCHED
#define DISPLAY_LONG_FORMAT     (DISPLAY_SCHED|DISPLAY_TIMES|DISPLAY_STATE)

static int display_format;

#define SORT_BY_PCPU     1
#define SORT_BY_ISW      2
#define SORT_BY_CPUTIME  4
#define SORT_BY_CTXSW    8
#define SORT_BY_RWA      16
#define SORT_REVERSE     128

static int sort_key;

#define ONE_BILLION 1000000000ULL
#define ONE_MILLION 1000000ULL

#define for_each_cpu(__cpu)				\
	for (__cpu = 0; __cpu < CPU_SETSIZE; __cpu++)	\
		if (CPU_ISSET(__cpu, &cpu_restrict))

#define short_optlist "@hnlstpc:S:"

static const struct option options[] = {
	{
		.name = "cpu",
		.has_arg = required_argument,
		.val = 'c',
	},
	{
		.name = "state",
		.has_arg = no_argument,
		.val = 's',
	},
	{
		.name = "times",
		.has_arg = no_argument,
		.val = 't',
	},
	{
		.name = "policy",
		.has_arg = no_argument,
		.val = 'p',
	},
	{
		.name = "long",
		.has_arg = no_argument,
		.val = 'l',
	},
	{
		.name = "numeric",
		.has_arg = no_argument,
		.val = 'n',
	},
	{
		.name = "sort",
		.has_arg = required_argument,
		.val = 'S',
	},
	{
		.name = "help",
		.has_arg = no_argument,
		.val = 'h',
	},
	{ /* Sentinel */ }
};

#define ARRAY_SIZE(__array)  (sizeof(__array) / sizeof((__array)[0]))

static struct thread_info {
	const char *name;
	int cpu;
	int bprio;
	int cprio;
	char *policy;
	char *policy_details;
	unsigned long nr_inbandsw;
	unsigned long nr_ctxsw;
	unsigned long nr_syscalls;
	unsigned long nr_rwakeups;
	unsigned long long cpu_time;
	unsigned long long timeout;
	int percent_cpu;
	unsigned int state;
	pid_t pid;
	bool parse_ok;
	struct thread_info *next;
} *thread_list;

static int thread_nr;

static bool collect_sched_info(struct thread_info *ti, char *buf)
{
	int ret = sscanf(buf, "%d %d %d %ms %ms",
			&ti->cpu, &ti->bprio, &ti->cprio,
			&ti->policy, &ti->policy_details);

	return ret >= 4 && ret <= 5;
}

static bool collect_statistics(struct thread_info *ti, char *buf)
{
	int ret = sscanf(buf, "%lu %lu %lu %lu %Lu %d",
			&ti->nr_inbandsw,
			&ti->nr_ctxsw,
			&ti->nr_syscalls,
			&ti->nr_rwakeups,
			&ti->cpu_time,
			&ti->percent_cpu);

	return ret == 5;
}

static bool collect_state_info(struct thread_info *ti, char *buf)
{
	int ret = sscanf(buf, "%x", &ti->state);

	return ret == 1;
}

static bool collect_pid(struct thread_info *ti, char *buf)
{
	int ret = sscanf(buf, "%d", &ti->pid);

	return ret == 1;
}

static bool collect_timeout(struct thread_info *ti, char *buf)
{
	int ret = sscanf(buf, "%Lu", &ti->timeout);

	return ret == 1;
}

static struct collect_handler {
	const char *name;
	bool (*handle)(struct thread_info *ti, char *buf);
} collect_handlers[] = {
	{
		.name = "sched",
		.handle = collect_sched_info,
	},
	{
		.name = "stats",
		.handle = collect_statistics,
	},
	{
		.name = "state",
		.handle = collect_state_info,
	},
	{
		.name = "pid",
		.handle = collect_pid,
	},
	{
		.name = "timeout",
		.handle = collect_timeout,
	},
};

static int scan_thread(const char *fpath, const struct stat *sb,
		int typeflag, struct FTW *ftwbuf)
{
	struct collect_handler *h;
	struct thread_info *ti;
	bool parse_ok = false;
	char *name, *buf, *p;
	ENTRY entry, *e;
	FILE *fp;
	int n;

	if (typeflag == FTW_D && !strcmp(fpath + ftwbuf->base, "clone"))
		return FTW_SKIP_SUBTREE;

	if (typeflag != FTW_F || ftwbuf->level != 2 || sb->st_size == 0)
		return FTW_CONTINUE;

	for (n = 0; n < ARRAY_SIZE(collect_handlers); n++) {
		h = collect_handlers + n;
		if (!strcmp(fpath + ftwbuf->base, h->name))
			break;
	}

	if (n >= ARRAY_SIZE(collect_handlers))
		return FTW_CONTINUE;

	buf = malloc(sb->st_size + 1); /* +EOL */
	if (buf == NULL)
		error(1, errno, "malloc()");

	/* Ignore stale threads, but mark them as !parse_ok. */
	fp = fopen(fpath, "r");
	if (fp) {
		if (fgets(buf, sb->st_size, fp)) {
			parse_ok = true;
			p = strrchr(buf, '\n');
			if (p)
				*p = '\0';
		}
		fclose(fp);
	}

	name = basename(dirname(strdup(fpath)));
	entry.key = name;
	entry.data = NULL;
	e = hsearch(entry, FIND);
	if (e == NULL) {
		ti = malloc(sizeof(*ti));
		if (ti == NULL)
			error(1, errno, "malloc()");
		memset(ti, 0, sizeof(*ti));
		ti->name = name;
		entry.data = ti;
		hsearch(entry, ENTER);
		ti->next = thread_list;
		thread_list = ti;
		thread_nr++;
	} else
		ti = e->data;

	if (parse_ok && !h->handle(ti, buf))
		parse_ok = false; /* wrong ABI format? */

	ti->parse_ok = parse_ok;

	free(buf);

	return FTW_CONTINUE;
}

static int compare_info(const void *lhs, const void *rhs)
{
	const struct thread_info *til = *((const struct thread_info **)lhs),
		*tir = *((const struct thread_info **)rhs);
	int ret;

	switch (sort_key & ~SORT_REVERSE) {
	case SORT_BY_PCPU:
		ret = (int)(til->percent_cpu - tir->percent_cpu);
		break;
	case SORT_BY_ISW:
		ret = (int)((long)til->nr_inbandsw - (long)tir->nr_inbandsw);
		break;
	case SORT_BY_CTXSW:
		ret = (int)((long)til->nr_ctxsw - (long)tir->nr_ctxsw);
		break;
	case SORT_BY_RWA:
		ret = (int)((long)til->nr_rwakeups - (long)tir->nr_rwakeups);
		break;
	case SORT_BY_CPUTIME:
		ret = (int)((long long)til->cpu_time - (long)tir->cpu_time);
		break;
	default:
		ret = (int)(til->pid - tir->pid); /* Defaults to pid */
	}

	if (sort_key & SORT_REVERSE)
		ret = -ret;

	return ret;
}

static void sort_thread_info(void)
{
	struct thread_info **sarray, *ti, *sorted_list, **last;
	int nr, n;

	if (thread_nr == 0)
		return;

	sarray = malloc(thread_nr * sizeof(ti));
	if (sarray == NULL)
		error(1, errno, "malloc()");

	/*
	 * We will be leaving as soon as the output is done, so let's
	 * leak memory happily for non-parsable entries.
	 */
	for (ti = thread_list, nr = 0; ti; ti = ti->next) {
		if (ti->parse_ok)
			sarray[nr++] = ti;
	}

	if (nr > 0) {
		qsort(sarray, nr, sizeof(ti), compare_info);
		sorted_list = NULL;
		last = &sorted_list;
		for (n = 0; n < nr; n++) {
			ti = sarray[n];
			*last = ti;
			ti->next = NULL;
			last = &ti->next;
		}
		thread_list = sorted_list;
	} else
		thread_list = NULL;

	thread_nr = nr;

	free(sarray);
}

static void collect_thread_info(void)
{
	const char *sysfs = getenv("EVL_SYSDIR");
	char *dir;
	int ret;

	if (sysfs == NULL)
		error(1, EINVAL, "EVL_SYSDIR unset -- use 'evl ps' instead");

	ret = asprintf(&dir, "%s/thread", sysfs);
	if (ret < 0)
		error(1, ENOMEM, "asprintf()");

	ret = hcreate(2048);
	if (!ret)
		error(1, errno, "hcreate()");

	ret = nftw(dir, scan_thread, 256, FTW_PHYS|FTW_ACTIONRETVAL);
	if (ret)
		error(1, errno, "cannot walk %s", dir);

	free(dir);
}

struct display_handler {
	const char *header;
	const char *header_fmt;
	void (*display_data)(struct thread_info *ti);
	struct display_handler *next;
};

static void display_pid(struct thread_info *ti)
{
	printf("%-6d", ti->pid);
}

static struct display_handler pid_handler = {
	.header = "PID",
	.header_fmt = "%-6s",
	.display_data = display_pid,
};

static void display_name(struct thread_info *ti)
{
	if (ti->state & T_USER)
		printf("%s", ti->name);
	else
		printf("[%s]", ti->name);
}

static struct display_handler name_handler = { /* Always last. */
	.header = "NAME",
	.header_fmt = "%s",
	.display_data = display_name,
};

static void display_cpu(struct thread_info *ti)
{
	printf("%3d   ", ti->cpu);
}

static struct display_handler cpu_handler = {
	.header = "CPU",
	.header_fmt = "%-6s",
	.display_data = display_cpu,
};

static void display_policy(struct thread_info *ti)
{
	printf("%5s   ", ti->policy);
}

static struct display_handler policy_handler = {
	.header = "SCHED",
	.header_fmt = "%-8s",
	.display_data = display_policy,
};

static void display_prio(struct thread_info *ti)
{
	printf("%3d   ", ti->cprio);
}

static struct display_handler prio_handler = {
	.header = "PRIO",
	.header_fmt = "%-6s",
	.display_data = display_prio,
};

static void display_time(struct thread_info *ti)
{
	int secs, msecs, usecs;
	unsigned long long t;

	secs = (int)(ti->cpu_time / ONE_BILLION);
	t = ti->cpu_time % ONE_BILLION;
	msecs = t / ONE_MILLION;
	t = (int)(ti->cpu_time % ONE_MILLION);
	usecs = (int)(t / 1000ULL);
	printf("%.2d:%.3d.%.3d  ", secs, msecs, usecs);
}

static struct display_handler time_handler = {
	.header = "CPUTIME",
	.header_fmt = "%-12s",
	.display_data = display_time,
};

static void display_percent_cpu(struct thread_info *ti)
{
	printf("%3d   ", ti->percent_cpu);
}

static struct display_handler percent_cpu_handler = {
	.header = "%CPU",
	.header_fmt = "%-6s",
	.display_data = display_percent_cpu,
};

static void display_timeout(struct thread_info *ti)
{
	const int bufsz = 32;
	char buf[bufsz], *p = buf;
	unsigned long long sec;
	unsigned long ms, us;
	int len = bufsz;

	if (ti->timeout >= 1000) {
		sec = ti->timeout / ONE_BILLION;
		ms = (ti->timeout % ONE_BILLION) / ONE_MILLION;
		us = (ti->timeout % ONE_MILLION) / 1000UL;
		if (sec) {
			p += snprintf(p, bufsz - (p - buf), "%Lu", sec);
			len = bufsz - (p - buf);
		}
		if (len > 0 && (ms || (sec && us))) {
			if (p > buf)
				*p++ = ':';
			p += snprintf(p, bufsz - (p - buf), "%.3lu", ms);
			len = bufsz - (p - buf);
		}
		if (len > 0 && us) {
			if (p > buf)
				*p++ = '.';
			p += snprintf(p, bufsz - (p - buf), "%.3lu", us);
		}
	} else
		strcpy(buf, "   -");

	printf("%-13s", buf);
}

static struct display_handler timeout_handler = {
	.header = "TIMEOUT",
	.header_fmt = "%-13s",
	.display_data = display_timeout,
};

static char *format_state(struct thread_info *ti, char *buf)
{
	static const char labels[] = EVL_THREAD_STATE_LABELS;
	unsigned int mask;
	int flag, c;
	char *wp;

	for (mask = ti->state, flag = 0, wp = buf; mask; mask >>= 1, flag++) {
		if (!(mask & 1))
			continue;

		c = labels[flag];

		switch (1 << flag) {
		case T_DELAY:
			/*
			 * Only report genuine delays here, not timed
			 * waits for resources.
			 */
			if (ti->state & T_PEND)
				continue;
			break;
		case T_PEND:
			/* Report timed waits with lowercase symbol. */
			if (ti->state & T_DELAY)
				c |= 0x20;
			break;
		default:
			if (c == '.')
				continue;
		}
		*wp++ = c;
	}

	*wp = '\0';

	return buf;
}

static void display_state(struct thread_info *ti)
{
	char buf[CHAR_BIT * sizeof(int) + 1]; /* UINT_WIDTH+1 */

	if (display_format & DISPLAY_NUMERIC)
		printf(" %-8x", ti->state);
	else
		printf(" %-8s", format_state(ti, buf));
}

static struct display_handler state_handler = {
	.header = "STAT",
	.header_fmt = "%-9s",
	.display_data = display_state,
};

static void display_inbandsw(struct thread_info *ti)
{
	printf("%-8ld", ti->nr_inbandsw);
}

static struct display_handler inbandsw_handler = {
	.header = "ISW",
	.header_fmt = "%-8s",
	.display_data = display_inbandsw,
};

static void display_ctxsw(struct thread_info *ti)
{
	printf("%-10ld", ti->nr_ctxsw);
}

static struct display_handler ctxsw_handler = {
	.header = "CTXSW",
	.header_fmt = "%-10s",
	.display_data = display_ctxsw,
};

static void display_syscall(struct thread_info *ti)
{
	printf("%-10ld", ti->nr_syscalls);
}

static struct display_handler syscall_handler = {
	.header = "SYS",
	.header_fmt = "%-10s",
	.display_data = display_syscall,
};

static void display_rwakeups(struct thread_info *ti)
{
	printf("%-10ld", ti->nr_rwakeups);
}

static struct display_handler rwakeups_handler = {
	.header = "RWA",
	.header_fmt = "%-10s",
	.display_data = display_rwakeups,
};

static void print_thread_info(void)
{
	int nr_restrict = CPU_COUNT(&cpu_restrict);
	struct display_handler *chain, *h;
	struct thread_info *ti;

	/*
	 * Prepare the display chain according to the format
	 * requested.
	 */
	chain = &cpu_handler;
	cpu_handler.next = &pid_handler;
	h = &pid_handler;
	if (display_format & DISPLAY_SCHED) {
		h->next = &policy_handler;
		h = &policy_handler;
		h->next = &prio_handler;
		h = &prio_handler;
	}
	if (display_format & DISPLAY_STATE) {
		h->next = &inbandsw_handler;
		h = &inbandsw_handler;
		h->next = &ctxsw_handler;
		h = &ctxsw_handler;
		h->next = &syscall_handler;
		h = &syscall_handler;
		h->next = &rwakeups_handler;
		h = &rwakeups_handler;
		h->next = &state_handler;
		h = &state_handler;
	}
	if (display_format & DISPLAY_TIMES) {
		h->next = &timeout_handler;
		h = &timeout_handler;
		h->next = &percent_cpu_handler;
		h = &percent_cpu_handler;
		h->next = &time_handler;
		h = &time_handler;
	}
	h->next = &name_handler;

	for (h = chain; h; h = h->next)
		printf(h->header_fmt, h->header);

	putchar('\n');

	for (ti = thread_list; ti; ti = ti->next) {
		if (nr_restrict > 0 && !CPU_ISSET(ti->cpu, &cpu_restrict))
			continue;
		for (h = chain; h; h = h->next) {
			h->display_data(ti);
		}
		putchar('\n');
	}
}

static void usage(char *arg0)
{
        fprintf(stderr, "usage: %s [options]:\n", basename(arg0));
        fprintf(stderr, "-c --cpu=<n>[,<n>]     restrict report to CPU(s)\n");
        fprintf(stderr, "-s --state             display thread state\n");
        fprintf(stderr, "-t --times             display CPU utilization\n");
        fprintf(stderr, "-p --policy            display scheduling policy\n");
        fprintf(stderr, "-l --long              long format, same as -stp\n");
        fprintf(stderr, "-n --numeric           numeric output for STAT\n");
        fprintf(stderr, "-S --sort=<c|i|t|x>    sort key: c=%%CPU, i=ISW, t=CPUTIME, x=CTXSW, w=RWA\n");
        fprintf(stderr, "-h --help              this help\n");
}

static inline int resolve_cpuid(const char *s)
{
	return isdigit(*s) ? atoi(s) : -1;
}

static void build_cpu_mask(const char *cpu_list)
{
	char *s, *n, *range, *range_p = NULL, *id, *id_r;
	int start, end, cpu, nr_cpus;

	nr_cpus = (int)sysconf(_SC_NPROCESSORS_CONF);
	if (nr_cpus < 0)
		error(1, errno, "sysconf(_SC_NPROCESSORS_CONF)");

	CPU_ZERO(&cpu_restrict);

	s = n = strdup(cpu_list);
	while ((range = strtok_r(n, ",", &range_p)) != NULL) {
		if (*range == '\0')
			continue;
		end = -1;
		if (range[strlen(range)-1] == '-')
			end = nr_cpus - 1;
		id = strtok_r(range, "-", &id_r);
		if (id) {
			start = resolve_cpuid(id);
			if (*range == '-') {
				end = start;
				start = 0;
			}
			id = strtok_r(NULL, "-", &id_r);
			if (id)
				end = resolve_cpuid(id);
			else if (end < 0)
				end = start;
			if (start < 0 || start >= nr_cpus ||
			    end < 0 || end >= nr_cpus)
				goto fail;
		} else {
			start = 0;
			end = nr_cpus - 1;
		}
		for (cpu = start; cpu <= end; cpu++)
			CPU_SET(cpu, &cpu_restrict);
		n = NULL;
	}

	free(s);

	return;
fail:
	error(1, EINVAL, "bad CPU number/range in '%s'", cpu_list);
	free(s);
}

int main(int argc, char *const argv[])
{
	bool reverse_sort = false;
	const char *p;
	int c;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 'c':
			build_cpu_mask(optarg);
			break;
		case 's':
			display_format |= DISPLAY_STATE;
			break;
		case 'p':
			display_format |= DISPLAY_SCHED;
			break;
		case 't':
			display_format |= DISPLAY_TIMES;
			break;
		case 'l':
			display_format |= DISPLAY_LONG_FORMAT;
			break;
		case 'n':
			display_format |= DISPLAY_NUMERIC;
			break;
		case 'S':
			for (p = optarg; *p; p++) {
				switch (*p) {
				case 'c':
					sort_key = SORT_BY_PCPU;
					break;
				case 'i':
					sort_key = SORT_BY_ISW;
					break;
				case 't':
					sort_key = SORT_BY_CPUTIME;
					break;
				case 'x':
					sort_key = SORT_BY_CTXSW;
					break;
				case 'w':
					sort_key = SORT_BY_RWA;
					break;
				case 'r':
					reverse_sort = true;
					break;
				default:
					error(1, EINVAL, "bad sort key '%c'", *p);
				}
			}
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		case '@':
			printf("report a snapshot of the current EVL threads\n");
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	if (access(EVL_CONTROL_DEV, 0) && errno == ENOENT)
		error(1, ENOSYS, "EVL core is not present/enabled");

	if (!(display_format & ~DISPLAY_MODIFIERS))
		display_format |= DISPLAY_DEFAULT_FORMAT;

	if (reverse_sort)
		sort_key |= SORT_REVERSE;

	collect_thread_info();
	sort_thread_info();
	print_thread_info();

	return 0;
}
