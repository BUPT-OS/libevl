/*
 * SPDX-License-Identifier: MIT
 *
 * Derived from Xenomai Cobalt's latency & autotune utilities
 * (http://git.xenomai.org/xenomai-3.git/)
 * Copyright (C) 2014 Gilles Chanteperdrix <gch@xenomai.org>
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include <memory.h>
#include <signal.h>
#include <error.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <evl/evl.h>
#include <uapi/evl/devices/latmus.h>
#include <uapi/evl/signal.h>

#define OOBCPUS_LIST  "/sys/devices/virtual/evl/control/cpus"

static int test_irqlat, test_klat, test_ulat;

static int reset, load = -1, background,
	verbosity = 1, abort_on_switch,
	abort_threshold;

static int tuning;

static time_t timeout;

static time_t start_time;

static unsigned int period = 1000; /* 1ms */

static int sampler_priority = 90;

static int sampler_cpu = -1;

static sigset_t sigmask;

static int latmus_fd = -1;

#define short_optlist "ikurLNqbamtp:A:T:v:l:g:H:P:c:"

static const struct option options[] = {
	{
		.name = "irq",
		.has_arg = no_argument,
		.val = 'i'
	},
	{
		.name = "kernel",
		.has_arg = no_argument,
		.val = 'k'
	},
	{
		.name = "user",
		.has_arg = no_argument,
		.val = 'u'
	},
	{
		.name = "reset",
		.has_arg = no_argument,
		.val = 'r'
	},
	{
		.name = "load",
		.has_arg = no_argument,
		.val = 'L'
	},
	{
		.name = "noload",
		.has_arg = no_argument,
		.val = 'N'
	},
	{
		.name = "quiet",
		.has_arg = no_argument,
		.val = 'q'
	},
	{
		.name = "background",
		.has_arg = no_argument,
		.val = 'b'
	},
	{
		.name = "mode-abort",
		.has_arg = no_argument,
		.val = 'a'
	},
	{
		.name = "measure",
		.has_arg = no_argument,
		.val = 'm',
	},
	{
		.name = "tune",
		.has_arg = no_argument,
		.val = 't',
	},
	{
		.name = "period",
		.has_arg = required_argument,
		.val = 'p',
	},
	{
		.name = "timeout",
		.has_arg = required_argument,
		.val = 'T',
	},
	{
		.name = "maxlat-abort",
		.has_arg = required_argument,
		.val = 'A',
	},
	{
		.name = "verbose",
		.has_arg = optional_argument,
		.val = 'v',
	},
	{
		.name = "lines",
		.has_arg = required_argument,
		.val = 'l',
	},
	{
		.name = "plot",
		.has_arg = optional_argument,
		.val = 'g',
	},
	{
		.name = "histogram",
		.has_arg = required_argument,
		.val = 'H',
	},
	{
		.name = "priority",
		.has_arg = required_argument,
		.val = 'P',
	},
	{
		.name = "cpu",
		.has_arg = required_argument,
		.val = 'c',
	},
	{ /* Sentinel */ }
};

static void *sampler_thread(void *arg)
{
	int ret, n = 0, efd;
	__u64 timestamp = 0;
	struct timespec now;
	__u32 mode;

	efd = evl_attach_self("lat-sampler:%d", getpid());
	if (efd < 0)
		error(1, -efd, "evl_attach_self() failed");

	mode = T_WOSS;
	ret = oob_ioctl(efd, EVL_THRIOC_SET_MODE, &mode);
	if (ret)
		error(1, errno, "ioctl(EVL_THRIOC_SET_MODE) failed");

	for (;;) {
		ret = oob_ioctl(latmus_fd, EVL_LATIOC_PULSE, &timestamp);
		if (ret) {
			if (errno != EPIPE)
				error(1, errno, "pulse failed");
			timestamp = 0; /* Next period. */
			n = 0;
		} else {
			n++;
			evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
			timestamp = (__u64)now.tv_sec * 1000000000 + now.tv_nsec;
		}
	}

	return NULL;
}

static void create_sampler(pthread_t *tid)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = sampler_priority;
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(tid, &attr, sampler_thread, NULL);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "sampling thread");
}

static void *load_thread(void *arg)
{
	ssize_t nbytes, ret;
	struct timespec rqt;
	int fdi, fdo;
	char buf[512];

	fdi = open("/dev/zero", O_RDONLY);
	if (fdi < 0)
		error(1, errno, "/dev/zero");

	fdo = open("/dev/null", O_WRONLY);
	if (fdi < 0)
		error(1, errno, "/dev/null");

	rqt.tv_sec = 0;
	rqt.tv_nsec = 2000000;

	for (;;) {
		clock_nanosleep(CLOCK_MONOTONIC, 0, &rqt, NULL);
		nbytes = read(fdi, buf, sizeof(buf));
		if (nbytes <= 0)
			error(1, EIO, "load streaming");
		if (nbytes > 0) {
			ret = write(fdo, buf, nbytes);
			(void)ret;
		}
	}

	return NULL;
}

static void create_load(pthread_t *tid)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = 1;
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(tid, &attr, load_thread, NULL);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "load thread");
}

#define ONE_BILLION	1000000000
#define TEN_MILLIONS	10000000

static int lat_xfd = -1;

static int data_lines = 21;

static int32_t *histogram;

static unsigned int histogram_cells = 200;

static unsigned int all_overruns;

static unsigned int all_switches;

static int32_t all_minlat = TEN_MILLIONS, all_maxlat = -TEN_MILLIONS;

static int64_t all_sum;

static int64_t all_samples;

static FILE *plot_fp;

static int context_type = EVL_LAT_USER;

const char *context_labels[] = {
	[EVL_LAT_IRQ] = "irq",
	[EVL_LAT_KERN] = "kernel",
	[EVL_LAT_USER] = "user",
};

static void __log_results(struct latmus_measurement *meas)
{
	if (meas->min_lat < all_minlat)
		all_minlat = meas->min_lat;
	if (meas->max_lat > all_maxlat) {
		all_maxlat = meas->max_lat;
		if (abort_threshold && all_maxlat > abort_threshold) {
			fprintf(stderr, "latency threshold is exceeded"
				" (%d >= %.3f), aborting.\n",
				abort_threshold,
				(double)all_maxlat / 1000.0);
			exit(102);
		}
	}

	all_sum += meas->sum_lat;
	all_samples += meas->samples;
	all_overruns += meas->overruns;
}

static void log_results(struct latmus_measurement *meas,
			unsigned int round)
{
	double min, avg, max, best, worst;
	bool oops = false;
	time_t now, dt;

	if (verbosity > 0 && data_lines && (round % data_lines) == 0) {
		time(&now);
		dt = now - start_time - 1; /* -1s warmup time */
		printf("RTT|  %.2ld:%.2ld:%.2ld  "
			"(%s, %u us period, priority %d, CPU%d)\n",
			dt / 3600, (dt / 60) % 60, dt % 60,
			context_labels[context_type], period,
			sampler_priority,
			sampler_cpu);
		printf("RTH|%11s|%11s|%11s|%8s|%6s|%11s|%11s\n",
		       "----lat min", "----lat avg",
		       "----lat max", "-overrun", "---msw",
		       "---lat best", "--lat worst");
	}

	__log_results(meas);
	min = (double)meas->min_lat / 1000.0;
	avg = (double)(meas->sum_lat / (int)meas->samples) / 1000.0;
	max = (double)meas->max_lat / 1000.0;
	best = (double)all_minlat / 1000.0;
	worst = (double)all_maxlat / 1000.0;

	/*
	 * A trivial check on the reported values, so that we detect
	 * and stop on obviously inconsistent results.
	 */
	if (min > max || min > avg || avg > max ||
		min > worst || max > worst || avg > worst ||
		best > worst || worst < best) {
		oops = true;
		verbosity = 1;
	}

	if (verbosity > 0)
		printf("RTD|%11.3f|%11.3f|%11.3f|%8d|%6u|%11.3f|%11.3f\n",
			min, avg, max,
			all_overruns, all_switches,
			best, worst);

	if (oops) {
		fprintf(stderr, "results look weird, aborting.\n");
		exit(103);
	}
}

static void *logger_thread(void *arg)
{
	struct latmus_measurement meas;
	ssize_t ret, round = 0;

	if (verbosity > 0)
		fprintf(stderr, "warming up on CPU%d...\n", sampler_cpu);
	else
		fprintf(stderr, "running quietly for %ld seconds on CPU%d\n",
			timeout, sampler_cpu);

	for (;;) {
		ret = read(lat_xfd, &meas, sizeof(meas));
		if (ret != sizeof(meas))
			break;
		log_results(&meas, round++);
	}

	return NULL;
}

static void create_logger(pthread_t *tid)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
	param.sched_priority = 0;
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(tid, &attr, logger_thread, NULL);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "logger thread");
}

static void *measurement_thread(void *arg)
{
	struct latmus_measurement_result mr;
	struct latmus_result result;
	int ret;

	ret = evl_attach_self("lat-measure:%d", getpid());
	if (ret < 0)
		error(1, -ret, "evl_attach_self() failed");

	mr.last = arg;
	mr.histogram = histogram;
	mr.len = histogram ? histogram_cells * sizeof(int32_t) : 0;

	result.data = &mr;
	result.len = sizeof(mr);

	/* Run test until signal. */
	ret = oob_ioctl(latmus_fd, EVL_LATIOC_RUN, &result);
	if (ret)
		error(1, errno, "measurement failed");

	return NULL;
}

static void dump_procinfo(const char *path)
{
	char buf[BUFSIZ];
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return;

	while (fgets(buf, sizeof(buf), fp))
		fprintf(plot_fp, "# %s", buf);

	fclose(fp);
}

static void dump_gnuplot(time_t duration)
{
	unsigned int start, stop;
	int n;

	if (all_samples == 0)
		return;

	dump_procinfo("/proc/version");
	dump_procinfo("/proc/cmdline");
	fprintf(plot_fp, "# libevl version: %s\n", libevl_version_string);
	fprintf(plot_fp, "# sampling period: %u microseconds\n", period);
	fprintf(plot_fp, "# context: %s\n", context_labels[context_type]);
	if (test_klat || test_ulat) {
		fprintf(plot_fp, "# thread priority: %d\n", sampler_priority);
		fprintf(plot_fp, "# thread affinity: CPU%d\n", sampler_cpu);
	}
	fprintf(plot_fp, "# duration (hhmmss): %.2ld:%.2ld:%.2ld\n",
		duration / 3600, (duration / 60) % 60, duration % 60);
	if (all_overruns > 0)
		fprintf(plot_fp, "# OVERRUNS: %u\n", all_overruns);
	if (all_switches > 0)
		fprintf(plot_fp, "# IN-BAND SWITCHES: %u\n", all_switches);
	fprintf(plot_fp, "# min latency: %.3f\n",
		(double)all_minlat / 1000.0);
	fprintf(plot_fp, "# avg latency: %.3f\n",
		(double)(all_sum / all_samples) / 1000.0);
	fprintf(plot_fp, "# max latency: %.3f\n",
		(double)all_maxlat / 1000.0);

	for (n = 0; n < histogram_cells && histogram[n] == 0; n++)
		;
	start = n;

	for (n = histogram_cells - 1; n >= 0 && histogram[n] == 0; n--)
		;
	stop = n;

	for (n = start; n <= stop; n++) /* +1: no zero cell. */
		fprintf(plot_fp, "%d %d\n", n, histogram[n] + 1);
}

static void do_measurement(int type)
{
	struct latmus_measurement last;
	pthread_t sampler, logger;
	struct latmus_setup setup;
	pthread_attr_t attr;
	pthread_t waiter;
	time_t duration;
	int ret, sig;

	context_type = type;

	if (plot_fp) {
		histogram = malloc(histogram_cells * sizeof(int32_t));
		if (histogram == NULL)
			error(1, ENOMEM, "cannot get memory");
	}

	lat_xfd = evl_new_xbuf(1024, 0, "lat-data:%d", getpid());
	if (lat_xfd < 0)
		error(1, -lat_xfd, "cannot create xbuf");

	create_logger(&logger);

	memset(&setup, 0, sizeof(setup));
	setup.type = type;
	setup.period = period * 1000ULL; /* ns */
	setup.priority = sampler_priority;
	setup.cpu = sampler_cpu;
	setup.u.measure.xfd = lat_xfd;
	setup.u.measure.hcells = histogram ? histogram_cells : 0;
	ret = ioctl(latmus_fd, EVL_LATIOC_MEASURE, &setup);
	if (ret)
		error(1, errno, "measurement setup failed");

	if (type == EVL_LAT_USER)
		create_sampler(&sampler);

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(&waiter, &attr, measurement_thread, &last);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "measurement_thread");

	sigwait(&sigmask, &sig);

	pthread_cancel(waiter);
	pthread_join(waiter, NULL);

	/* Add results from the last incomplete bulk. */
	if (last.samples > 0)
		__log_results(&last);

	duration = time(NULL) - start_time - 1;
	if (plot_fp) {
		dump_gnuplot(duration); /* -1s warmup time */
		if (plot_fp != stdout)
			fclose(plot_fp);
		free(histogram);
	}

	if (!timeout)
		timeout = duration;

	if (all_samples > 0)
		printf("---|-----------|-----------|-----------|--------"
			"|------|-------------------------\n"
			"RTS|%11.3f|%11.3f|%11.3f|%8d|%6u|    "
			"%.2ld:%.2ld:%.2ld/%.2ld:%.2ld:%.2ld\n",
			(double)all_minlat / 1000.0,
			(double)(all_sum / all_samples) / 1000.0,
			(double)all_maxlat / 1000.0,
			all_overruns, all_switches,
			duration / 3600, (duration / 60) % 60,
			duration % 60, duration / 3600,
			(timeout / 60) % 60, timeout % 60);

	if (all_switches > 0)
		printf("WARNING: unexpected switches to in-band"
		       " mode detected, latency figures may not "
		       " be reliable. Please report.\n");

	abort_on_switch = 0;

	if (type == EVL_LAT_USER)
		pthread_cancel(sampler);

	if (verbosity)
		pthread_cancel(logger);
}

static void do_tuning(int type)
{
	struct latmus_result result;
	struct latmus_setup setup;
	pthread_t sampler;
	__s32 gravity;
	int ret;

	if (verbosity) {
		printf("%s gravity...", context_labels[type]);
		fflush(stdout);
	}

	memset(&setup, 0, sizeof(setup));
	setup.type = type;
	setup.period = period * 1000ULL; /* ns */
	setup.priority = sampler_priority;
	setup.cpu = sampler_cpu;
	setup.u.tune.verbosity = verbosity;
	ret = ioctl(latmus_fd, EVL_LATIOC_TUNE, &setup);
	if (ret)
		error(1, errno, "tuning setup failed (%s)", context_labels[type]);

	if (type == EVL_LAT_USER)
		create_sampler(&sampler);

	pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);

	result.data = &gravity;
	result.len = sizeof(gravity);
	ret = oob_ioctl(latmus_fd, EVL_LATIOC_RUN, &result);
	if (ret)
		error(1, errno, "measurement failed");

	if (type == EVL_LAT_USER)
		pthread_cancel(sampler);

	if (verbosity)
		printf("%u ns\n", gravity);
}

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	if (sigdebug_marked(si)) {
		switch (sigdebug_cause(si)) {
		case SIGDEBUG_MIGRATE_SIGNAL:
		case SIGDEBUG_MIGRATE_SYSCALL:
		case SIGDEBUG_MIGRATE_FAULT:
		case SIGDEBUG_MIGRATE_PRIOINV:
			if (abort_on_switch)
				exit(100);
			all_switches++;
			break;
		case SIGDEBUG_WATCHDOG:
		case SIGDEBUG_MUTEX_IMBALANCE:
		case SIGDEBUG_MUTEX_SLEEP:
		default:
			exit(99);
		}
	}
}

static void usage(void)
{
        fprintf(stderr, "usage: latmus [options]:\n");
        fprintf(stderr, "-m --measure            measure latency continuously [default]\n");
        fprintf(stderr, "-t --tune               tune the core timer\n");
        fprintf(stderr, "-i --irq                measure/tune interrupt latency\n");
        fprintf(stderr, "-k --kernel             measure/tune kernel scheduling latency\n");
        fprintf(stderr, "-u --user               measure/tune user scheduling latency\n");
        fprintf(stderr, "    [ if none of --irq, --kernel and --user is given,\n"
                        "      tune for all contexts ]\n");
        fprintf(stderr, "-p --period=<us>        sampling period\n");
        fprintf(stderr, "-P --priority=<prio>    sampler thread priority [=90]\n");
        fprintf(stderr, "-c --cpu=<n>            pin sampler thread to CPU [=0]\n");
        fprintf(stderr, "-r --reset              reset core timer gravity to factory default\n");
        fprintf(stderr, "-L --load               enable load generation [on if tuning]\n");
        fprintf(stderr, "-n --noload             disable load generation\n");
        fprintf(stderr, "-b --background         run in the background (daemon mode)\n");
        fprintf(stderr, "-a --mode-abort         abort upon unexpected switch to in-band mode\n");
        fprintf(stderr, "-A --max-abort=<us>     abort if maximum latency exceeds threshold\n");
        fprintf(stderr, "-T --timeout=<t>[dhms]  stop measurement after <t> [d(ays)|h(ours)|m(inutes)|s(econds)]\n");
        fprintf(stderr, "-v --verbose[=level]    set verbosity level [=1]\n");
        fprintf(stderr, "-q --quiet              quiet mode (i.e. --verbose=0)\n");
        fprintf(stderr, "-l --lines=<num>        result lines per page, 0 = no pagination [=21]\n");
        fprintf(stderr, "-H --histogram[=<nr>]   set histogram size to <nr> cells [=200]\n");
        fprintf(stderr, "-g --plot=<filename>    dump histogram data to file (gnuplot format)\n");
}

int main(int argc, char *const argv[])
{
	const char *plot_filename = NULL;
	int ret, c, spec, type, max_prio, fd;
	char *endptr, buf[BUFSIZ];
	struct sigaction sa;
	pthread_t loadgen;
	cpu_set_t cpu_set;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 'i':
			test_irqlat = 1;
			break;
		case 'k':
			test_klat = 1;
			break;
		case 'u':
			test_ulat = 1;
			break;
		case 'r':
			reset = 1;
			break;
		case 'L':
			load = 1;
			break;
		case 'N':
			load = 0;
			break;
		case 'q':
			verbosity = 0;
			break;
		case 'b':
			background = 1;
			break;
		case 'a':
			abort_on_switch = 1;
			break;
		case 'm':
			tuning = 0;
			break;
		case 't':
			tuning = 1;
			break;
		case 'p':
			period = atoi(optarg);
			if (period <= 0 || period > 1000000)
				error(1, EINVAL, "invalid sampling period "
				      "(0 < period < 1000000)");
			break;
		case 'A':
			abort_threshold = atoi(optarg) * 1000; /* ns */
			if (abort_threshold <= 0)
				error(1, EINVAL, "invalid timeout");
			break;
		case 'T':
			timeout = (int)strtol(optarg, &endptr, 10);
			if (timeout < 0 || endptr == optarg)
				error(1, EINVAL, "invalid timeout");
			switch (*endptr) {
			case 'd':
				timeout *= 24;
				/* Falldown wanted. */
			case 'h':
				timeout *= 60;
				/* Falldown wanted. */
			case 'm':
				timeout *= 60;
				break;
			case 's':
			case '\0':
				break;
			default:
				error(1, EINVAL, "invalid modifier: '%c'",
					*endptr);
			}
			alarm(timeout + 1); /* +1s warmup time */
			break;
		case 'v':
			verbosity = optarg ? atoi(optarg) : 1;
			break;
		case 'l':
			data_lines = atoi(optarg);
			break;
		case 'g':
			if (optarg && strcmp(optarg, "-"))
				plot_filename = optarg;
			else
				plot_fp = stdout;
			break;
		case 'H':
			histogram_cells = atoi(optarg);
			if (histogram_cells < 1 || histogram_cells > 1000)
				error(1, EINVAL, "invalid number of histogram cells "
				      "(0 < cells <= 1000)");
			break;
		case 'P':
			max_prio = sched_get_priority_max(SCHED_FIFO);
			sampler_priority = atoi(optarg);
			if (sampler_priority < 0 || sampler_priority > max_prio)
				error(1, EINVAL, "invalid thread priority "
				      "(0 < priority < %d)", max_prio);
			break;
		case 'c':
			sampler_cpu = atoi(optarg);
			if (sampler_cpu < 0 || sampler_cpu >= CPU_SETSIZE)
				error(1, EINVAL, "invalid CPU number");
			break;
		case '?':
		default:
			usage();
			return 1;
		}
	}

	if (optind < argc) {
		usage();
		return 1;
	}

	if (sampler_cpu < 0) {	/* Pick a default. */
		*buf = '\0';
		fd = open(OOBCPUS_LIST, O_RDONLY);
		if (fd < 0 || read(fd, buf, sizeof(buf)) < 0)
			error(1, errno, "cannot read %s", OOBCPUS_LIST);
		close(fd);
		/* Pick the first one of the list or range. */
		sampler_cpu = atoi(buf);
	}

	setlinebuf(stdout);
	setlinebuf(stderr);

	if (!tuning && !timeout && !verbosity) {
		fprintf(stderr, "--quiet requires --timeout, ignoring --quiet\n");
		verbosity = 1;
	}

	if (background && verbosity) {
		fprintf(stderr, "--background requires --quiet, taming verbosity down\n");
		verbosity = 0;
	}

	if (tuning && (plot_filename || plot_fp)) {
		fprintf(stderr, "--plot implies --measure, ignoring --plot\n");
		plot_filename = NULL;
		plot_fp = NULL;
	}

	CPU_ZERO(&cpu_set);
	CPU_SET(sampler_cpu, &cpu_set);
	ret = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	if (ret)
		error(1, errno, "cannot set affinity to CPU%d",
		      sampler_cpu);

	if (background) {
		signal(SIGHUP, SIG_IGN);
		ret = daemon(0, 0);
		if (ret)
			error(1, errno, "cannot daemonize");
	}

	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGHUP);
	sigaddset(&sigmask, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	latmus_fd = open("/dev/latmus", O_RDWR);
	if (latmus_fd < 0)
		error(1, errno, "cannot open latmus device");

	spec = test_irqlat || test_klat || test_ulat;
	if (!tuning) {
		if (!spec)
			test_ulat = 1;
		else if (test_irqlat + test_klat + test_ulat > 1)
			error(1, EINVAL, "only one of --user, --kernel or "
			      "--irq in measurement mode");
	} else if (!spec)
		test_irqlat = test_klat = test_ulat = 1;

	if (reset) {
		ret = ioctl(latmus_fd, EVL_LATIOC_RESET);
		if (ret)
			error(1, errno, "reset failed");
	}

	/* Force load if tuning, otherwise consider option. */
	if (load > 0 || tuning) {
		load = 1;
		create_load(&loadgen);
	}

	time(&start_time);

	if (!tuning) {
		if (plot_filename) {
			if (!access(plot_filename, F_OK))
				error(1, EINVAL, "declining to overwrite %s",
				      plot_filename);
			plot_fp = fopen(plot_filename, "w");
			if (plot_fp == NULL)
				error(1, errno, "cannot open %s for writing",
				      plot_filename);
		}
		type = test_irqlat ? EVL_LAT_IRQ : test_klat ?
			EVL_LAT_KERN : EVL_LAT_USER;
		do_measurement(type);
	} else {
		if (verbosity)
			printf("== latmus started for core tuning, "
			       "period=%d microseconds (may take a while)\n",
			       period);

		ret = evl_attach_self("lat-tuner:%d", getpid());
		if (ret < 0)
			error(1, -ret, "evl_attach_self() failed");

		if (test_irqlat)
			do_tuning(EVL_LAT_IRQ);

		if (test_klat)
			do_tuning(EVL_LAT_KERN);

		if (test_ulat)
			do_tuning(EVL_LAT_USER);

		if (verbosity)
			printf("== tuning completed after %ds\n",
			       (int)(time(NULL) - start_time));
	}

	if (load > 0)
		pthread_cancel(loadgen);

	return 0;
}
