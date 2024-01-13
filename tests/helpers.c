/*
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include <evl/thread.h>
#include "helpers.h"

#define ISOLATED_CPU_LIST "/sys/devices/system/cpu/isolated"
#define OOB_CPU_LIST	  "/sys/devices/virtual/evl/control/cpus"

char *get_unique_name_and_path(const char *type,
			int serial, char **ppath)
{
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/rros/%s/test%d.%d",
		       type, getpid(), serial);
	if (ret < 0)
		error(1, ENOMEM, "malloc");

	/*
	 * Since we need a path, this has to be a public element, so
	 * we want the slash in.
	 */
	if (ppath) {
		*ppath = path;
		return strrchr(path, '/');
	}

	/* That one is private, skip the leading slash. */
	return strrchr(path, '/') + 1;
}

void new_thread(pthread_t *tid, int policy, int prio,
		void *(*fn)(void *), void *arg)
{
	struct sched_param param;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	param.sched_priority = prio;
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	pthread_attr_setschedpolicy(&attr, policy);
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	__Texpr_assert(pthread_create(tid, &attr, fn, arg) == 0);
}

void timespec_add_ns(struct timespec *__restrict r,
		const struct timespec *__restrict t,
		long ns)
{
	long s, rem;

	s = ns / 1000000000;
	rem = ns - s * 1000000000;
	r->tv_sec = t->tv_sec + s;
	r->tv_nsec = t->tv_nsec + rem;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}

static void parse_cpu_list(const char *path, cpu_set_t *cpuset)
{
	char *p, *range, *range_p = NULL, *id, *id_r;
	int start, end, cpu;
	char buf[BUFSIZ];
	FILE *fp;

	CPU_ZERO(cpuset);

	fp = fopen(path, "r");
	if (fp == NULL)
		return;

	if (!fgets(buf, sizeof(buf), fp))
		goto out;

	p = buf;
	while ((range = strtok_r(p, ",", &range_p)) != NULL) {
		if (*range == '\0' || *range == '\n')
			goto next;
		end = -1;
		id = strtok_r(range, "-", &id_r);
		if (id) {
			start = atoi(id);
			id = strtok_r(NULL, "-", &id_r);
			if (id)
				end = atoi(id);
			else if (end < 0)
				end = start;
			for (cpu = start; cpu <= end; cpu++)
				CPU_SET(cpu, cpuset);
		}
	next:
		p = NULL;
	}
out:
	fclose(fp);
}

int pick_test_cpu(int hint_cpu, bool inband_test, bool *isolated)
{
	cpu_set_t isolated_cpus, oob_cpus, best_cpus;
	int cpu;

	parse_cpu_list(ISOLATED_CPU_LIST, &isolated_cpus);
	parse_cpu_list(OOB_CPU_LIST, &oob_cpus);

	if (hint_cpu >= 0) {
		/* The hint is not oob-capable, pick a better one. */
		if (!inband_test && !CPU_ISSET(hint_cpu, &oob_cpus))
			goto pick_oob;
		goto finish;
	}

	if (inband_test)
		goto pick_isolated;

	/*
	 * Pick a default CPU among the ones which are both
	 * OOB-capable and isolated. If EVL is not enabled, oob_cpus
	 * is empty so there is no best choice.
	 */
	CPU_AND(&best_cpus, &isolated_cpus, &oob_cpus);
	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (CPU_ISSET(cpu, &best_cpus)) {
			hint_cpu = cpu;
			goto finish;
		}
	}

	/*
	 * If no best choice, pick the first OOB-capable CPU we can
	 * find (if any).
	 */
pick_oob:
	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (CPU_ISSET(cpu, &oob_cpus)) {
			hint_cpu = cpu;
			goto finish;
		}
	}

pick_isolated:
	/*
	 * This must be a kernel with no EVL support or we
	 * specifically need an isolated CPU.
	 */
	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (CPU_ISSET(cpu, &isolated_cpus)) {
			hint_cpu = cpu;
			goto finish;
		}
	}

	/* Out of luck, run on the current CPU. */
	if (hint_cpu < 0)
		hint_cpu = sched_getcpu();
finish:
	if (isolated)
		*isolated = CPU_ISSET(hint_cpu, &isolated_cpus);

	return hint_cpu;
}
