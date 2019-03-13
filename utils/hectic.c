/*
 * SPDX-License-Identifier: MIT
 *
 * Derived from Xenomai Cobalt's switchtest program
 * (http://git.xenomai.org/xenomai-3.git/)
 * Copyright (C) 2006-2013 Gilles Chanteperdrix <gch@xenomai.org>
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <ctype.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <evl/evl.h>
#include <uapi/asm/evl/fptest.h>
#include <uapi/evl/devices/hectic.h>

static unsigned int nr_cpus;

static cpu_set_t cpu_affinity;

#define for_each_cpu(__cpu)				\
	for (__cpu = 0; __cpu < CPU_SETSIZE; __cpu++)	\
		if (CPU_ISSET(__cpu, &cpu_affinity))

#define for_each_cpu_index(__cpu, __index)				\
	for (__cpu = 0, __index = -1; __cpu < CPU_SETSIZE; __cpu++)	\
		if (CPU_ISSET(__cpu, &cpu_affinity) && ++__index >= 0)

/* Thread type. */
typedef enum {
	SLEEPER = 0,
	RTK  = 1,	 /* kernel-space thread. */
	RTUP = 2,	 /* user-space real-time thread ruuning OOB. */
	RTUS = 3,	 /* user-space real-time thread running in-band. */
	RTUO = 4,	 /* user-space real-time thread oscillating
			    between in-band and OOB contexts. */
	SWITCHER = 8,
	FPU_STRESS = 16,
} threadtype;

typedef enum {
	UFPP = 1,	 /* use the FPU while running OOB. */
	UFPS = 2	 /* use the FPU while running in-band. */
} fpflags;

struct cpu_tasks;

struct task_params {
	threadtype type;
	fpflags fp;
	pthread_t thread;
	struct cpu_tasks *cpu;
	struct hectic_task_index swt;
};

struct cpu_tasks {
	unsigned int index;
	struct task_params *tasks;
	unsigned tasks_count;
	unsigned capacity;
	unsigned fd;
	unsigned long last_switches_count;
};

static sem_t sleeper_start;
static int quiet, status;
static struct timespec start;
static pthread_mutex_t headers_lock;
static unsigned long data_lines = 21;
static __u32 fp_features;
static pthread_t main_tid;

static inline void clean_exit(int retval)
{
	status = retval;
        pthread_kill(main_tid, SIGTERM);
	for (;;)
		/* Wait for cancellation. */
		sem_wait(&sleeper_start);
}

static void timespec_substract(struct timespec *result,
			const struct timespec *lhs,
			const struct timespec *rhs)
{
	result->tv_sec = lhs->tv_sec - rhs->tv_sec;
	if (lhs->tv_nsec >= rhs->tv_nsec)
		result->tv_nsec = lhs->tv_nsec - rhs->tv_nsec;
	else {
		result->tv_sec -= 1;
		result->tv_nsec = lhs->tv_nsec + (1000000000 - rhs->tv_nsec);
	}
}

static char *task_name(char *buf, size_t sz,
		       struct cpu_tasks *cpu, unsigned task)
{
	char *basename [] = {
		[SLEEPER] = "sleeper",
		[RTK] = "rtk",
		[RTUP] = "rtup",
		[RTUS] = "rtus",
		[RTUO] = "rtuo",
		[SWITCHER] = "switcher",
		[FPU_STRESS] = "fpu_stress",
	};
	struct {
		unsigned flag;
		char *name;
	} flags [] = {
		{ .flag = UFPP, .name = "ufpp" },
		{ .flag = UFPS, .name = "ufps" },
	};
	struct task_params *param;
	unsigned pos, i;

	if (task > cpu->tasks_count)
		return "???";

	if (task == cpu->tasks_count)
		param = &cpu->tasks[task];
	else
		for (param = &cpu->tasks[0]; param->swt.index != task; param++)
			;

	pos = snprintf(buf, sz, "%s", basename[param->type]);
	for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
		if (!(param->fp & flags[i].flag))
			continue;

		pos += snprintf(&buf[pos],
				sz - pos, "_%s", flags[i].name);
	}

	pos += snprintf(&buf[pos], sz - pos, "%u", cpu->index);

	snprintf(&buf[pos], sz - pos, "-%u", param->swt.index);

	return buf;
}

static void handle_bad_fpreg(struct cpu_tasks *cpu, unsigned fp_val,
			     int bad_reg)
{
	struct hectic_error err;
	unsigned from, to;
	char buffer[64];

	ioctl(cpu->fd, EVL_HECIOC_GET_LAST_ERROR, &err);

	if (fp_val == ~0)
		fp_val = err.fp_val;

	from = err.last_switch.from;
	to = err.last_switch.to;

	if (bad_reg < 0)
		fprintf(stderr, "FPU corruption detected");
	else
		fprintf(stderr, "fpreg%d corrupted", bad_reg);
	fprintf(stderr, " after context switch from task %d(%s) ",
		from, task_name(buffer, sizeof(buffer), cpu, from));
	fprintf(stderr, "to task %d(%s),\nFPU registers were set to %u ",
		to, task_name(buffer, sizeof(buffer), cpu, to), fp_val);
	fp_val %= 1000;
	if (fp_val < 500)
		fprintf(stderr, "(maybe task %s)\n",
			task_name(buffer, sizeof(buffer), cpu, fp_val));
	else {
		fp_val -= 500;
		if (fp_val > cpu->tasks_count)
			fprintf(stderr, "(unidentified task)\n");
		else
			fprintf(stderr, "(maybe task %s, having used fpu in "
				"kernel-space)\n",
				task_name(buffer, sizeof(buffer), cpu, fp_val));
	}

	clean_exit(EXIT_FAILURE);
}

static void display_cleanup(void *cookie)
{
	pthread_mutex_t *mutex = cookie;
	pthread_mutex_unlock(mutex);
}

static void display_switches_count(struct cpu_tasks *cpu, struct timespec *now)
{
	static unsigned nlines = 0;
	__u32 switches_count;

	if (cpu->tasks_count < 2) /* Nothing but the switcher/sleeper */
		return;

	if (ioctl(cpu->fd,
		  EVL_HECIOC_GET_SWITCHES_COUNT,&switches_count)) {
		perror("sleeper: ioctl(EVL_HECIOC_GET_SWITCHES_COUNT)");
		clean_exit(EXIT_FAILURE);
	}

	if (switches_count &&
	    switches_count == cpu->last_switches_count) {
		fprintf(stderr, "No context switches during one second, "
			"aborting.\n");
		clean_exit(EXIT_FAILURE);
	}

	if (quiet)
		return;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(display_cleanup, &headers_lock);
	pthread_mutex_lock(&headers_lock);

	if (data_lines && (nlines++ % data_lines) == 0) {
		struct timespec diff;
		long dt;

		timespec_substract(&diff, now, &start);
		dt = diff.tv_sec;

		printf("RTT|  %.2ld:%.2ld:%.2ld\n",
		       dt / 3600, (dt / 60) % 60, dt % 60);
		printf("RTH|%12s|%12s|%12s\n",
		       "---------cpu","ctx switches","-------total");
	}

	printf("RTD|%12u|%12lu|%12u\n", cpu->index,
	       switches_count - cpu->last_switches_count, switches_count);

	pthread_cleanup_pop(1);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	cpu->last_switches_count = switches_count;
}

static void *sleeper_switcher(void *cookie)
{
	struct task_params *param = cookie;
	unsigned to, tasks_count = param->cpu->tasks_count;
	struct timespec ts, last;
	int fd = param->cpu->fd, bad_reg;
	struct hectic_switch_req rtsw;
	cpu_set_t cpu_set;
	unsigned i = 1;		/* Start at 1 to avoid returning to a
				   non-existing task. */
	int ret;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("sleeper: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.from = param->swt.index;
	to = param->swt.index;

	ts.tv_sec = 0;
	ts.tv_nsec = 1000000;

	ret = sem_wait(&sleeper_start);
	if (ret) {
		fprintf(stderr, "sem_wait FAILED (%d)\n", errno);
		fflush(stderr);
		exit(77);
	}

	clock_gettime(CLOCK_REALTIME, &last);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	for (;;) {
		struct timespec now, diff;
		unsigned expected, fp_val;
		int err;
		if (param->type == SLEEPER)
			nanosleep(&ts, NULL);

		clock_gettime(CLOCK_REALTIME, &now);

		timespec_substract(&diff, &now, &last);
		if (diff.tv_sec >= 1) {
			last = now;

			display_switches_count(param->cpu, &now);
		}

		if (tasks_count == 1)
			continue;

		switch (i % 3) {
		case 0:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case 1:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if (param->fp & UFPS)
			evl_set_fpregs(fp_features, expected);
		err = ioctl(fd, EVL_HECIOC_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = ioctl(fd, EVL_HECIOC_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0, -1);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if (param->fp & UFPS) {
			fp_val = evl_check_fpregs(fp_features, expected, bad_reg);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val, bad_reg);
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}


static double dot(volatile double *a, volatile double *b, int n)
{
    int k = n - 1;
    double s = 0.0;
    for(; k >= 0; k--)
	s = s + a[k]*b[k];

    return s;
}

static void *fpu_stress(void *cookie)
{
	static volatile double a[10000], b[sizeof(a)/sizeof(a[0])];
	struct task_params *param = (struct task_params *) cookie;
	cpu_set_t cpu_set;
	unsigned i;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("sleeper: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	for (i = 0; i < sizeof(a)/sizeof(a[0]); i++)
		a[i] = b[i] = 3.14;

	for (;;) {
		double s = dot(a, b, sizeof(a)/sizeof(a[0]));
		if ((unsigned) (s + 0.5) != 98596) {
			fprintf(stderr, "fpu stress task failure! dot: %g\n", s);
			clean_exit(EXIT_FAILURE);
		}
		pthread_testcancel();
	}

	return NULL;
}

static void attach_thread(struct task_params *param)
{
	char buffer[64];
	int efd;

	task_name(buffer, sizeof(buffer), param->cpu,param->swt.index);

	efd = evl_attach_self("%s:%d", buffer, getpid());
	if (efd < 0) {
		perror("evl_attach()");
		clean_exit(EXIT_FAILURE);
	}
}

static void *rtup(void *cookie)
{
	struct task_params *param = cookie;
	unsigned to, tasks_count = param->cpu->tasks_count;
	int err, fd = param->cpu->fd, bad_reg;
	struct hectic_switch_req rtsw;
	cpu_set_t cpu_set;
	unsigned i = 0;

	attach_thread(param);

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("rtup: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.from = param->swt.index;
	to = param->swt.index;

	do {
		err = oob_ioctl(fd, EVL_HECIOC_PEND, &param->swt);
	} while (err == -1 && errno == EINTR);

	if (err == -1)
		return NULL;

	for (;;) {
		unsigned expected, fp_val;

		switch (i % 3) {
		case 0:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case 1:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if (param->fp & UFPP)
			evl_set_fpregs(fp_features, expected);
		err = oob_ioctl(fd, EVL_HECIOC_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = oob_ioctl(fd, EVL_HECIOC_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0, -1);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if (param->fp & UFPP) {
			fp_val = evl_check_fpregs(fp_features, expected, bad_reg);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val, bad_reg);
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}

static void *rtus(void *cookie)
{
	struct task_params *param = cookie;
	unsigned to, tasks_count = param->cpu->tasks_count;
	int err, fd = param->cpu->fd, bad_reg;
	struct hectic_switch_req rtsw;
	cpu_set_t cpu_set;
	unsigned i = 0;

	attach_thread(param);

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("rtus: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.from = param->swt.index;
	to = param->swt.index;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	do {
		err = ioctl(fd, EVL_HECIOC_PEND, &param->swt);
	} while (err == -1 && errno == EINTR);

	if (err == -1)
		return NULL;

	for (;;) {
		unsigned expected, fp_val;

		switch (i % 3) {
		case 0:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case 1:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if (param->fp & UFPS)
			evl_set_fpregs(fp_features, expected);
		err = ioctl(fd, EVL_HECIOC_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = ioctl(fd, EVL_HECIOC_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0, -1);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if (param->fp & UFPS) {
			fp_val = evl_check_fpregs(fp_features, expected, bad_reg);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val, bad_reg);
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}

/*
 * Oscillation between in-band and OOB contexts relies on two
 * fundamental EVL properties:
 *
 * - the core will switch an EVL thread to OOB context automatically
 * before handling any oob_* syscall from it.
 *
 * - EVL threads lazily switch to the appropriate context upon syscall
 * only, except those belonging to the SCHED_WEAK class which are
 * eagerly switched back to in-band mode upon return from
 * syscall. This means that SCHED_FIFO threads stay in the current
 * in-band/OOB context until they have to switch upon oob_ioctl/ioctl
 * syscalls.
 */
static void *rtuo(void *cookie)
{
	struct task_params *param = cookie;
	unsigned int mode, to, tasks_count = param->cpu->tasks_count;
	int (*svc)(int fd, unsigned long request, ...);
	int err, fd = param->cpu->fd, bad_reg;
	struct hectic_switch_req rtsw;
	cpu_set_t cpu_set;
	unsigned i = 0;

	attach_thread(param);

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("rtuo: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.from = param->swt.index;
	to = param->swt.index;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	svc = oob_ioctl;
	mode = 1;

	do {
		err = svc(fd, EVL_HECIOC_PEND, &param->swt);
	} while (err == -1 && errno == EINTR);

	if (err == -1)
		return NULL;

	for (;;) {
		unsigned expected, fp_val;

		switch (i % 3) {
		case 0:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case 1:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if ((mode && param->fp & UFPP) || (!mode && param->fp & UFPS))
			evl_set_fpregs(fp_features, expected);
		err = svc(fd, EVL_HECIOC_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = svc(fd, EVL_HECIOC_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0, -1);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if ((mode && param->fp & UFPP) || (!mode && param->fp & UFPS)) {
			fp_val = evl_check_fpregs(fp_features, expected, bad_reg);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val, bad_reg);
		}

		/* Switch mode. */
		if (i % 3 == 2) {
			mode = 3 - mode;
			svc = mode & 1 ? oob_ioctl : ioctl;
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}

static int parse_arg(struct task_params *param,
		     const char *text,
		     struct cpu_tasks *cpus)
{
	struct t2f {
		const char *text;
		unsigned flag;
	};

	static struct t2f type2flags [] = {
		{ "rtk",  RTK  },
		{ "rtup", RTUP },
		{ "rtus", RTUS },
		{ "rtuo", RTUO }
	};

	static struct t2f fp2flags [] = {
		{ "_ufpp", UFPP },
		{ "_ufps", UFPS }
	};

	unsigned long cpu;
	char *cpu_end;
	unsigned i;
	int n;

	param->type = param->fp = 0;
	param->cpu = &cpus[0];

	for(i = 0; i < sizeof(type2flags)/sizeof(struct t2f); i++) {
		size_t len = strlen(type2flags[i].text);

		if(!strncmp(text, type2flags[i].text, len)) {
			param->type = type2flags[i].flag;
			text += len;
			goto fpflags;
		}
	}

	return -1;

  fpflags:
	if (*text == '\0')
		return 0;

	if (isdigit(*text))
		goto cpu_nr;

	for(i = 0; i < sizeof(fp2flags)/sizeof(struct t2f); i++) {
		size_t len = strlen(fp2flags[i].text);

		if(!strncmp(text, fp2flags[i].text, len)) {
			param->fp |= fp2flags[i].flag;
			text += len;

			goto fpflags;
		}
	}

	return -1;

  cpu_nr:
	cpu = strtoul(text, &cpu_end, 0);

	if (*cpu_end != '\0' || (cpu == ULONG_MAX && errno))
		return -1;

	param->cpu = &cpus[nr_cpus]; /* Invalid at first. */
	for_each_cpu_index(i, n)
		if (i == cpu) {
			param->cpu = &cpus[n];
			break;
		}

	return 0;
}

static int check_arg(const struct task_params *param, struct cpu_tasks *end_cpu)
{
	if (param->cpu > end_cpu - 1)
		return 0;

	switch (param->type) {
	case SLEEPER:
	case SWITCHER:
	case FPU_STRESS:
		break;

	case RTK:
		if (param->fp & (UFPS|UFPP))
			return 0;
		break;

	case RTUP:
		if (param->fp & UFPS)
			return 0;
		break;

	case RTUS:
		if (param->fp & UFPP)
			return 0;
		break;

	case RTUO:
		break;

	default:
		return 0;
	}

	return 1;
}

static int task_create(struct cpu_tasks *cpu,
		       struct task_params *param,
		       pthread_attr_t *rt_attr)
{
	typedef void *thread_routine(void *);
	thread_routine *task_routine [] = {
		[RTUP] = rtup,
		[RTUS] = rtus,
		[RTUO] = rtuo
	};
	pthread_attr_t attr;
	int err;

	param->swt.flags = 0;

	switch(param->type) {
	case RTK:
		err = ioctl(cpu->fd, EVL_HECIOC_CREATE_KTASK, &param->swt);
		if (err) {
			perror("ioctl(EVL_HECIOC_CREATE_KTASK)");
			return -1;
		}
		break;

	case RTUP:
	case RTUS:
	case RTUO:
		param->swt.flags = HECTIC_OOB_WAIT;
		/* fallthrough wanted. */
	case SLEEPER:
	case SWITCHER:
		err = ioctl(cpu->fd, EVL_HECIOC_REGISTER_UTASK, &param->swt);
		if (err) {
			perror("ioctl(EVL_HECIOC_REGISTER_UTASK)");
			return -1;
		}
		break;

	case FPU_STRESS:
		break;

	default:
		fprintf(stderr, "Invalid task type %d. Aborting\n", param->type);
		return EINVAL;
	}

	if (param->type == RTK)
		return 0;

	if (param->type == SLEEPER || param->type == SWITCHER) {
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
		err = pthread_create(&param->thread,
				     &attr,
				     sleeper_switcher,
				     param);
		pthread_attr_destroy(&attr);
		if (err)
			fprintf(stderr,"pthread_create: %s\n",strerror(err));

		return err;
	}

	if (param->type == FPU_STRESS) {
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
		err = pthread_create(&param->thread,
				     &attr,
				     fpu_stress,
				     param);
		pthread_attr_destroy(&attr);
		if (err)
			fprintf(stderr,"pthread_create: %s\n",strerror(err));


		return err;
	}

	err = pthread_create(&param->thread, rt_attr,
			     task_routine[param->type], param);
	if (err) {
		fprintf(stderr, "pthread_create: %s\n", strerror(err));
		return err;
	}

	return 0;
}

static int open_rttest(unsigned int count)
{
	int fd, ret;

	fd = open("/dev/hectic", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "hectic: cannot open /dev/hectic\n"
			"(modprobe hectic?)\n");
		return -1;
	}

	ret = ioctl(fd, EVL_HECIOC_SET_TASKS_COUNT, count);
	if (ret) {
		fprintf(stderr, "hectic: ioctl: %m\n");
		return -1;
	}

	return fd;
}

const char *all_nofp [] = {
	"rtk",
	"rtk",
	"rtup",
	"rtup",
	"rtus",
	"rtus",
	"rtuo",
	"rtuo",
};

const char *all_fp [] = {
	"rtk",
	"rtk",
	"rtup",
	"rtup",
	"rtup_ufpp",
	"rtup_ufpp",
	"rtus",
	"rtus",
	"rtus_ufps",
	"rtus_ufps",
	"rtuo",
	"rtuo",
	"rtuo_ufpp",
	"rtuo_ufpp",
	"rtuo_ufps",
	"rtuo_ufps",
	"rtuo_ufpp_ufps",
	"rtuo_ufpp_ufps"
};

static unsigned long xatoul(const char *str)
{
	unsigned long result;
	char *endptr;

	result = strtoul(str, &endptr, 0);

	if (result == ULONG_MAX && errno == ERANGE) {
		fprintf(stderr, "Overflow while parsing %s\n", str);
		exit(EXIT_FAILURE);
	}

	if (*endptr != '\0') {
		fprintf(stderr, "Error while parsing \"%s\" as a number\n", str);
		exit(EXIT_FAILURE);
	}

	return result;
}

static void usage(FILE *fd, const char *progname)
{
	unsigned i, j;

	fprintf(fd,
		"Usage:\n"
		"%s [options] threadspec threadspec...\n"
		"Create threads of various types and attempt to switch context "
		"between these\nthreads, printing the count of context switches "
		"every second.\n\n"
		"Available options are:\n"
		"--help or -h, cause this program to print this help string and "
		"exit;\n"
		"--lines <lines> or -l <lines> print headers every <lines> "
		"lines.\n"
		"--quiet or -q, prevent this program from printing every "
		"second the count of\ncontext switches;\n"
		"--really-quiet or -Q, prevent this program from printing any output;\n"
		"--timeout <duration> or -T <duration>, limit the test duration "
		"to <duration>\nseconds;\n"
		"--nofpu or -n, disables any use of FPU instructions.\n"
		"--stress <period> or -s <period> enable a stress mode where:\n"
		"  context switches occur every <period> us;\n"
		"  a background task uses fpu (and check) fpu all the time.\n"
		"Each 'threadspec' specifies the characteristics of a "
		"thread to be created:\n"
		"threadspec = (rtup|rtus|rtuo)[_ufpp|_ufps]*[0-9]* or rtk[0-9]*\n"
		"rtk for a kernel-space real-time thread;\n"
		"rtup for a user-space real-time thread running in primary"
		" mode,\n"
		"rtus for a user-space real-time thread running in secondary"
		" mode,\n"
		"rtuo for a user-space real-time thread oscillating between"
		" primary and\nsecondary mode,\n\n"
		"_ufpp means that the created thread will use the FPU when in "
		"primary mode\n(invalid for rtus),\n"
		"_ufps means that the created thread will use the FPU when in "
		"secondary mode\n(invalid for rtk and rtup),\n\n"
		"[0-9]* specifies the ID of the CPU where the created thread "
		"will run, 0 if\nunspecified.\n\n"
		"Passing no 'threadspec' is equivalent to running:\n%s",
		progname, progname);

	for_each_cpu(i) {
		for (j = 0; j < sizeof(all_fp)/sizeof(char *); j++)
			fprintf(fd, " %s%d", all_fp[j], i);
	}

	fprintf(fd,
		"\n\nPassing only the --nofpu or -n argument is equivalent to "
		"running:\n%s", progname);

	for_each_cpu(i) {
		for (j = 0; j < sizeof(all_nofp)/sizeof(char *); j++)
			fprintf(fd, " %s%d", all_nofp[j], i);
	}
	fprintf(fd, "\n\n");
}

static sigjmp_buf jump;

static void illegal_instruction(int sig)
{
	signal(sig, SIG_DFL);
	siglongjmp(jump, 1);
}

/*
 * We run the fpu check in a dedicated thread to avoid clobbering the
 * main thread's fpu context. Doing so is important for covering all
 * test cases including when some threads do not use the fpu at all,
 * which would not happen if main()'s children threads inherit an
 * originally clobbered fpu context from their parent.
 */
static void *check_fpu_thread(void *cookie)
{
	unsigned int fp_val;
	int bad_reg;

	/* Check if fp routines are dummy or if hw fpu is not supported. */
	if (quiet < 2)
		fprintf(stderr, "== Testing FPU check routines...\n");

	if(sigsetjmp(jump, 1)) {
		if (quiet < 2)
			fprintf(stderr,
			"== Hardware FPU not available on your board"
			" or not enabled in Linux kernel\n== configuration:"
			" skipping FPU switches tests.\n");
		return NULL;
	}
	signal(SIGILL, illegal_instruction);
	evl_set_fpregs(fp_features, 1);
	fp_val = evl_check_fpregs(fp_features, 2, bad_reg);
	(void)bad_reg;
	signal(SIGILL, SIG_DFL);
	if (fp_val != 1) {
		if (quiet < 2)
			fprintf(stderr,
				"== FPU check routines: unimplemented, "
				"skipping FPU switches tests.\n");
		return NULL;
	}

	if (quiet < 2)
		fprintf(stderr, "== FPU check routines: OK.\n");

	return (void *) 1;
}

static int check_fpu(void)
{
	pthread_attr_t attr;
	pthread_t tid;
	void *status;
	int err;

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	err = pthread_create(&tid, &attr, check_fpu_thread, NULL);
	pthread_attr_destroy(&attr);
	if (err) {
		fprintf(stderr, "pthread_create: %s\n", strerror(err));
		exit(EXIT_FAILURE);
	}

	err = pthread_join(tid, &status);
	if (err) {
		fprintf(stderr, "pthread_join: %s\n", strerror(err));
		exit(EXIT_FAILURE);
	}

	return (long) status;
}

static inline int resolve_cpuid(const char *s)
{
	return isdigit(*s) ? atoi(s) : -1;
}

static void build_cpu_mask(const char *cpu_list)
{
	char *s, *n, *range, *range_p = NULL, *id, *id_r;
	int start, end, cpu, nr_cpus, ret;

	nr_cpus = (int)sysconf(_SC_NPROCESSORS_CONF);
	if (nr_cpus < 0)
		error(1, errno, "sysconf(_SC_NPROCESSORS_CONF)");

	CPU_ZERO(&cpu_affinity);

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
			CPU_SET(cpu, &cpu_affinity);
		n = NULL;
	}

	free(s);

	ret = sched_setaffinity(0, sizeof(cpu_affinity), &cpu_affinity);
	if (ret)
		error(1, EINVAL, "invalid CPU in '%s'", cpu_list);

	return;
fail:
	error(1, EINVAL, "invalid CPU number/range in '%s'", cpu_list);
	free(s);
}

int main(int argc, const char *argv[])
{
	unsigned i, j, n, use_fp = 1, stress = 0;
	const char *progname = argv[0];
	pthread_attr_t rt_attr;
	struct cpu_tasks *cpus;
	struct sched_param sp;
	sigset_t mask;
	int sig;

	status = EXIT_SUCCESS;
	main_tid = pthread_self();

	if (sem_init(&sleeper_start, 0, 0)) {
		perror("sem_init");
		exit(EXIT_FAILURE);
	}

	fp_features = evl_detect_fpu();

	opterr = 0;
	for (;;) {
		static struct option long_options[] = {
			{ "help",    0, NULL, 'h' },
			{ "lines",   1, NULL, 'l' },
			{ "nofpu",   0, NULL, 'n' },
			{ "quiet",   0, NULL, 'q' },
			{ "really-quiet", 0, NULL, 'Q' },
			{ "stress",  1, NULL, 's' },
			{ "timeout", 1, NULL, 'T' },
			{ "cpu", 1, NULL, 'c' },
			{ NULL,      0, NULL, 0   }
		};
		int i = 0;
		int c = getopt_long(argc, (char *const *) argv, "hl:nqQs:T:c:",
				    long_options, &i);

		if (c == -1)
			break;

		switch(c) {

		case 'h':
			usage(stdout, progname);
			exit(EXIT_SUCCESS);

		case 'l':
			data_lines = xatoul(optarg);
			break;

		case 'n':
			use_fp = 0;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'Q':
			quiet = 2;
			break;

		case 's':
			stress = xatoul(optarg);
			break;

		case 'T':
			alarm(xatoul(optarg));
			break;

		case 'c':
			build_cpu_mask(optarg);
			break;

		case '?':
			usage(stderr, progname);
			fprintf(stderr, "%s: Invalid option.\n", argv[optind-1]);
			exit(EXIT_FAILURE);

		case ':':
			usage(stderr, progname);
			fprintf(stderr, "Missing argument of option %s.\n",
				argv[optind-1]);
			exit(EXIT_FAILURE);
		}
	}

	nr_cpus = CPU_COUNT(&cpu_affinity);
	if (nr_cpus == 0) {
		nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
		if (nr_cpus < 0)
			error(1, errno, "sysconf(_SC_NPROCESSORS_ONLN)");
		for (i = 0; i < nr_cpus; i++)
			CPU_SET(i, &cpu_affinity);
	}

	if (setvbuf(stdout, NULL, _IOLBF, 0)) {
		perror("setvbuf");
		exit(EXIT_FAILURE);
	}

	/* If no argument was passed (or only -n), replace argc and argv with
	   default values, given by all_fp or all_nofp depending on the presence
	   of the -n flag. */
	if (optind == argc) {
		const char **all;
		char buffer[32];
		unsigned count;

		if (use_fp)
			use_fp = check_fpu();

		if (use_fp) {
			all = all_fp;
			count = sizeof(all_fp)/sizeof(char *);
		} else {
			all = all_nofp;
			count = sizeof(all_nofp)/sizeof(char *);
		}

		argc = count * nr_cpus + 1;
		argv = (const char **) malloc(argc * sizeof(char *));
		argv[0] = progname;
		for_each_cpu_index(i, n) {
			for (j = 0; j < count; j++) {
				snprintf(buffer,
					 sizeof(buffer),
					 "%s%d",
					 all[j],
					 i);
				argv[n * count + j + 1] = strdup(buffer);
			}
		}

		optind = 1;
	}

	cpus = malloc(sizeof(*cpus) * nr_cpus);
	if (!cpus) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	for_each_cpu_index(i, n) {
		size_t size;
		cpus[n].fd = -1;
		cpus[n].index = i;
		cpus[n].capacity = 2;
		size = cpus[n].capacity * sizeof(struct task_params);
		cpus[n].tasks_count = 1;
		cpus[n].tasks = (struct task_params *) malloc(size);
		cpus[n].last_switches_count = 0;

		if (!cpus[n].tasks) {
			perror("malloc");
			exit(EXIT_FAILURE);
		}

		cpus[n].tasks[0].type = stress ? SWITCHER : SLEEPER;
		cpus[n].tasks[0].fp = use_fp ? UFPS : 0;
		cpus[n].tasks[0].cpu = &cpus[n];
		cpus[n].tasks[0].thread = 0;
		cpus[n].tasks[0].swt.index = cpus[n].tasks[0].swt.flags = 0;
	}

	/* Parse arguments and build data structures. */
	for(i = optind; i < argc; i++) {
		struct task_params params;
		struct cpu_tasks *cpu;

		if(parse_arg(&params, argv[i], cpus)) {
			usage(stderr, progname);
			fprintf(stderr, "Unable to parse %s as a thread type. "
				"Aborting.\n", argv[i]);
			exit(EXIT_FAILURE);
		}

		if (!check_arg(&params, &cpus[nr_cpus])) {
			usage(stderr, progname);
			fprintf(stderr,
				"Invalid parameters %s. Aborting\n",
				argv[i]);
			exit(EXIT_FAILURE);
		}

		if (!use_fp && params.fp) {
			usage(stderr, progname);
			fprintf(stderr,
				"%s is invalid because FPU is disabled"
				" (option -n passed).\n", argv[i]);
			exit(EXIT_FAILURE);
		}

		cpu = params.cpu;
		if(++cpu->tasks_count > cpu->capacity) {
			size_t size;
			cpu->capacity += cpu->capacity / 2;
			size = cpu->capacity * sizeof(struct task_params);
			cpu->tasks =
				(struct task_params *) realloc(cpu->tasks, size);
			if (!cpu->tasks) {
				perror("realloc");
				exit(EXIT_FAILURE);
			}
		}

		params.thread = 0;
		params.swt.index = params.swt.flags = 0;
		cpu->tasks[cpu->tasks_count - 1] = params;
	}

	if (stress)
		for_each_cpu_index(i, n) {
			struct task_params params;
			struct cpu_tasks *cpu = &cpus[n];

			if (cpu->tasks_count + 1 > cpu->capacity) {
				size_t size;
				cpu->capacity += cpu->capacity / 2;
				size = cpu->capacity * sizeof(struct task_params);
				cpu->tasks = realloc(cpu->tasks, size);
				if (!cpu->tasks) {
					perror("realloc");
					exit(EXIT_FAILURE);
				}
			}

			params.type = FPU_STRESS;
			params.fp = UFPS;
			params.cpu = cpu;
			params.thread = 0;
			params.swt.index = cpu->tasks_count;
			params.swt.flags = 0;
			cpu->tasks[cpu->tasks_count] = params;
		}

	/* For best compatibility with both LinuxThreads and NPTL, block the
	   termination signals on all threads. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	pthread_mutex_init(&headers_lock, NULL);

	/* Prepare attributes to be inherited by EVL threads. */
	pthread_attr_init(&rt_attr);
	pthread_attr_setinheritsched(&rt_attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&rt_attr, SCHED_FIFO);
	sp.sched_priority = 1;
	pthread_attr_setschedparam(&rt_attr, &sp);
	pthread_attr_setstacksize(&rt_attr, EVL_STACK_DEFAULT);

	if (quiet < 2)
		printf("== Threads:");

	/* Create and register all tasks. */
	for_each_cpu_index(i, n) {
		struct cpu_tasks *cpu = &cpus[n];
		char buffer[64];

		cpu->fd = open_rttest(cpu->tasks_count);

		if (cpu->fd == -1)
			goto failure;

		if (ioctl(cpu->fd, EVL_HECIOC_SET_CPU, i)) {
			perror("ioctl(EVL_HECIOC_SET_CPU)");
			goto failure;
		}

		if (stress &&
		    ioctl(cpu->fd, EVL_HECIOC_SET_PAUSE, stress)) {
			perror("ioctl(EVL_HECIOC_SET_PAUSE)");
			goto failure;
		}

		for (j = 0; j < cpu->tasks_count + !!stress; j++) {
			struct task_params *param = &cpu->tasks[j];
			if (task_create(cpu, param, &rt_attr)) {
			  failure:
				status = EXIT_FAILURE;
				goto cleanup;
			}
			if (quiet < 2)
				printf(" %s",
				       task_name(buffer, sizeof(buffer),
						 param->cpu, param->swt.index));
		}
	}
	if (quiet < 2)
		printf("\n");

	clock_gettime(CLOCK_REALTIME, &start);

	/* Start the sleeper tasks. */
	for (i = 0; i < nr_cpus; i ++)
		sem_post(&sleeper_start);

	/* Wait for interruption. */
	sigwait(&mask, &sig);

	/* Allow a second Ctrl-C in case of lockup. */
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

	/* Cleanup. */
  cleanup:
	for_each_cpu_index(i, n) {
		struct cpu_tasks *cpu = &cpus[n];

		/* kill the user-space tasks. */
		for (j = 0; j < cpu->tasks_count + !!stress; j++) {
			struct task_params *param = &cpu->tasks[j];

			if (param->type != RTK && param->thread)
				pthread_cancel(param->thread);
		}
	}

	for_each_cpu_index(i, n) {
		struct cpu_tasks *cpu = &cpus[n];

		/* join the user-space tasks. */
		for (j = 0; j < cpu->tasks_count + !!stress; j++) {
			struct task_params *param = &cpu->tasks[j];

			if (param->type != RTK && param->thread)
				pthread_join(param->thread, NULL);
		}

		if (cpu->fd != -1) {
			struct timespec now;

			clock_gettime(CLOCK_REALTIME, &now);

			if (quiet == 1)
				quiet = 0;
			display_switches_count(cpu, &now);

			/* Kill the kernel-space tasks. */
			close(cpu->fd);
		}
		free(cpu->tasks);
	}
	free(cpus);
	sem_destroy(&sleeper_start);
	pthread_mutex_destroy(&headers_lock);

	return status;
}
