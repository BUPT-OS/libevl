/*
 * SPDX-License-Identifier: MIT
 *
 * Derived from Xenomai Cobalt's latency & autotune utilities
 * (http://git.xenomai.org/xenomai-3.git/)
 * Copyright (C) 2014 Gilles Chanteperdrix <gch@xenomai.org>
 * Copyright (C) 2018-2020 Philippe Gerum  <rpm@xenomai.org>
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <getopt.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include <memory.h>
#include <poll.h>
#include <signal.h>
#include <error.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <evl/evl.h>
#include <linux/gpio.h>
#include <uapi/evl/devices/latmus.h>
#include <uapi/evl/devices/gpio.h>
#include <uapi/evl/signal.h>
#include <latmon.h>	/* Zephyr-based latmon interface. */

#define ISOLATED_CPU_LIST "/sys/devices/system/cpu/isolated"
#define OOB_CPU_LIST	  "/sys/devices/virtual/evl/control/cpus"

static cpu_set_t isolated_cpus;

#define OOB_GPIO_LAT    1
#define INBAND_GPIO_LAT 2

#define LATMON_TIMEOUT_SECS  5

static int test_irqlat, test_klat,
	test_ulat, test_gpiolat;

static bool reset, background,
	abort_on_switch = true;

static int verbosity = 1,
	abort_threshold;

static int tuning;

static time_t timeout;

static time_t start_time;

static unsigned int period_usecs = 1000; /* 1ms */

static int responder_priority = 99;

static int responder_cpu = -1;

static int responder_cpu_state;

static struct in_addr gpio_monitor_ip;

static sigset_t sigmask;

static int latmus_fd = -1;

static int gpio_infd = -1, gpio_outfd = -1;

static int gpio_inpin, gpio_outpin;

static int gpio_hdinflags = GPIOHANDLE_REQUEST_INPUT,
	gpio_hdoutflags = GPIOHANDLE_REQUEST_OUTPUT;

static int gpio_evinflags;

static pthread_t responder, logger;

static sem_t logger_done;

static bool c_state_restricted;

#define short_optlist "ikurqbKmtp:A:T:v::l:g::H:P:c:Z:z:I:O:"

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
		.name = "keep-going",
		.has_arg = no_argument,
		.val = 'K'
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
	{
		.name = "oob-gpio",
		.has_arg = required_argument,
		.val = 'Z',
	},
	{
		.name = "inband-gpio",
		.has_arg = required_argument,
		.val = 'z',
	},
	{
		.name = "gpio-in",
		.has_arg = required_argument,
		.val = 'I',
	},
	{
		.name = "gpio-out",
		.has_arg = required_argument,
		.val = 'O',
	},
	{ /* Sentinel */ }
};

static void create_responder(pthread_t *tid, void *(*responder)(void *))
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = responder_priority;
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(tid, &attr, responder, NULL);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "sampling thread");
}

#define ONE_BILLION	1000000000
#define TEN_MILLIONS	10000000

static int lat_xfd = -1;

static int lat_sock = -1;

static int data_lines = 21;

static int32_t *histogram;

static unsigned int histogram_cells = 200;

static unsigned int all_overruns;

static unsigned int spurious_inband_switches;

static int32_t all_minlat = TEN_MILLIONS, all_maxlat = -TEN_MILLIONS;

static int64_t all_sum;

static int64_t all_samples;

static time_t peak_time;

static FILE *plot_fp;

#define EVL_LAT_OOB_GPIO      (EVL_LAT_USER + 1) /* yeah, well... */
#define EVL_LAT_INBAND_GPIO   (EVL_LAT_OOB_GPIO + 1) /* ditto. */

static int context_type = EVL_LAT_USER;

const char *context_labels[] = {
	[EVL_LAT_IRQ] = "irq",
	[EVL_LAT_KERN] = "kernel",
	[EVL_LAT_USER] = "user",
	[EVL_LAT_OOB_GPIO] = "oob-gpio",
	[EVL_LAT_INBAND_GPIO] = "inband-gpio",
};

static void __log_results(struct latmus_measurement *meas)
{
	if (meas->min_lat < all_minlat)
		all_minlat = meas->min_lat;
	if (meas->max_lat > all_maxlat) {
		peak_time = time(NULL) - start_time - 1;
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
			"(%s, %u us period, priority %d, CPU%d%s)\n",
			dt / 3600, (dt / 60) % 60, dt % 60,
			context_labels[context_type], period_usecs,
			responder_priority,
			responder_cpu,
			responder_cpu_state & EVL_CPU_ISOL ? "" : "-noisol");
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
			all_overruns, spurious_inband_switches,
			best, worst);

	if (oops) {
		fprintf(stderr, "results look weird, aborting.\n");
		exit(103);
	}
}

static void create_logger(pthread_t *tid, void *(*logger)(void *), void *arg)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	sem_init(&logger_done, 0, 0);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
	param.sched_priority = 0;
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(tid, &attr, logger, arg);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "logger thread");
}

static void *xbuf_logger_thread(void *arg)
{
	struct latmus_measurement meas;
	ssize_t ret, round = 0;

	for (;;) {
		ret = read(lat_xfd, &meas, sizeof(meas));
		if (ret != sizeof(meas))
			break;
		log_results(&meas, round++);
	}

	/* Nobody waits for logger_done in timer mode. */

	return NULL;
}

static void *timer_responder(void *arg)
{
	__u64 timestamp = 0;
	struct timespec now;
	int ret, efd;

	/* Make it a public thread only for demo purpose. */
	efd = evl_attach_self("/timer-responder:%d", getpid());
	if (efd < 0)
		error(1, -efd, "evl_attach_self() failed");

	ret = evl_set_thread_mode(efd, T_WOSS, NULL);
	if (ret)
		error(1, -ret, "evl_set_thread_mode(T_WOSS) failed");

	for (;;) {
		ret = oob_ioctl(latmus_fd, EVL_LATIOC_PULSE, &timestamp);
		if (ret) {
			if (errno != EPIPE)
				error(1, errno, "pulse failed");
			timestamp = 0; /* Next period. */
		} else {
			evl_read_clock(EVL_CLOCK_MONOTONIC, &now);
			timestamp = (__u64)now.tv_sec * 1000000000 + now.tv_nsec;
		}
	}

	return NULL;
}

static void *timer_test_sitter(void *arg)
{
	struct latmus_measurement_result mr;
	struct latmus_measurement last;
	struct latmus_result result;
	int ret;

	/*
	 * Keep this service thread private by omitting the initial
	 * slash character in the name.
	 */
	ret = evl_attach_self("test-sitter:%d", getpid());
	if (ret < 0)
		error(1, -ret, "evl_attach_self() failed");

	mr.last_ptr = (__u64)(uintptr_t)&last;
	mr.histogram_ptr = (__u64)(uintptr_t)histogram;
	mr.len = histogram ? histogram_cells * sizeof(int32_t) : 0;

	result.data_ptr = (__u64)(uintptr_t)&mr;
	result.len = sizeof(mr);

	/* Run test until signal. */
	ret = oob_ioctl(latmus_fd, EVL_LATIOC_RUN, &result);
	if (ret)
		error(1, errno, "measurement failed");

	/* Add results from the last incomplete bulk. */
	if (last.samples > 0)
		__log_results(&last);

	return NULL;
}

static void setup_measurement_on_timer(void)
{
	struct latmus_setup setup;
	pthread_attr_t attr;
	pthread_t sitter;
	int ret, sig;

	lat_xfd = evl_create_xbuf(1024, 0, 0, "lat-data:%d", getpid());
	if (lat_xfd < 0)
		error(1, -lat_xfd, "cannot create xbuf");

	create_logger(&logger, xbuf_logger_thread, NULL);

	memset(&setup, 0, sizeof(setup));
	setup.type = context_type;
	setup.period = period_usecs * 1000ULL; /* ns */
	setup.priority = responder_priority;
	setup.cpu = responder_cpu;
	setup.u.measure.xfd = lat_xfd;
	setup.u.measure.hcells = histogram ? histogram_cells : 0;
	ret = ioctl(latmus_fd, EVL_LATIOC_MEASURE, &setup);
	if (ret)
		error(1, errno, "measurement setup failed");

	if (context_type == EVL_LAT_USER)
		create_responder(&responder, timer_responder);

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, EVL_STACK_DEFAULT);
	ret = pthread_create(&sitter, &attr, timer_test_sitter, NULL);
	pthread_attr_destroy(&attr);
	if (ret)
		error(1, ret, "timer_test_sitter");

	sigwait(&sigmask, &sig);
	pthread_cancel(sitter);
	pthread_join(sitter, NULL);
}

static void setup_gpio_pins(int *fds)
{
	struct gpiohandle_request out;
	struct gpioevent_request in;
	int ret;

	in.handleflags = gpio_hdinflags;
	in.eventflags = gpio_evinflags;
	in.lineoffset = gpio_inpin;
	strcpy(in.consumer_label, "latmon-pulse");

	ret = ioctl(gpio_infd, GPIO_GET_LINEEVENT_IOCTL, &in);
	if (ret)
		error(1, errno, "ioctl(GPIO_GET_LINEEVENT_IOCTL)");

	out.lineoffsets[0] = gpio_outpin;
        out.lines = 1;
	out.flags = gpio_hdoutflags;
        out.default_values[0] = 1;
	strcpy(out.consumer_label, "latmon-ack");

	ret = ioctl(gpio_outfd, GPIO_GET_LINEHANDLE_IOCTL, &out);
	if (ret)
		error(1, errno, "ioctl(GPIO_GET_LINEHANDLE_IOCTL)");

	fds[0] = in.fd;
	fds[1] = out.fd;
}

static void *gpio_responder_thread(void *arg)
{
	struct gpiohandle_data data = { 0 };
	struct gpioevent_data event;
	typeof(ioctl) *do_ioctl;
	typeof(read) *do_read;
	const int ackval = 0;	/* Remote observes falling edges. */
	int fds[2], efd, ret;

	setup_gpio_pins(fds);

	if (context_type == EVL_LAT_OOB_GPIO) {
		efd = evl_attach_self("/gpio-responder:%d", getpid());
		if (efd < 0)
			error(1, -efd, "evl_attach_self() failed");

		ret = evl_set_thread_mode(efd, T_WOSS, NULL);
		if (ret)
			error(1, -ret, "evl_set_thread_mode(T_WOSS) failed");

		do_ioctl = oob_ioctl;
		do_read = oob_read;
	} else {
		do_ioctl = ioctl;
		do_read = read;
	}

	for (;;) {
		data.values[0] = !ackval;
		ret = do_ioctl(fds[1], GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
		if (ret)
			error(1, errno,
			"ioctl(GPIOHANDLE_SET_LINE_VALUES_IOCTL) failed");

		ret = do_read(fds[0], &event, sizeof(event));
		if (ret != sizeof(event))
			break;

		data.values[0] = ackval;
		ret = do_ioctl(fds[1], GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
		if (ret)
			error(1, errno,
				"ioctl(GPIOHANDLE_SET_LINE_VALUES_IOCTL) failed");
	}

	return NULL;
}

static ssize_t read_net_data(void *buf, size_t len)
{
	ssize_t count = 0, ret;
	struct pollfd pollfd;

	pollfd.fd = lat_sock;
	pollfd.events = POLLIN;
	pollfd.revents = 0;

	do {
		/* Make sure to detect latmon unresponsivess. */
		ret = poll(&pollfd, 1, LATMON_TIMEOUT_SECS * 1000);
		if (ret <= 0)
			return -ETIMEDOUT;
		ret = recv(lat_sock, buf + count, len - count, 0);
		if (ret <= 0)
			return ret;
		count += ret;
	} while (count < len);

	return count;
}

static void *sock_logger_thread(void *arg)
{
	struct latmus_measurement meas;
	struct latmon_net_data ndata;
	bool *no_response = arg;
	ssize_t ret, round = 0;
	int cell;

	for (;;) {
		ret = read_net_data(&ndata, sizeof(ndata));
		if (ret <= 0)
			goto unresponsive;

		/*
		 * Receiving an empty data record means that we got
		 * the trailing data in the previous round, so
		 * we are done with sample bulks now.
		 */
		if (ndata.samples == 0)
			break;

		/* This is valid sample data, log it. */
		meas.sum_lat = ((__s64)ntohl(ndata.sum_lat_hi)) << 32 |
			ntohl(ndata.sum_lat_lo);
		meas.min_lat = ntohl(ndata.min_lat);
		meas.max_lat = ntohl(ndata.max_lat);
		meas.overruns = ntohl(ndata.overruns);
		meas.samples = ntohl(ndata.samples);
		log_results(&meas, round++);
	}

	if (histogram == NULL)
		goto out;

	ret = read_net_data(histogram, histogram_cells * sizeof(int32_t));
	if (ret <= 0) {
	unresponsive:
		*no_response = true;
		kill(getpid(), SIGHUP);
	} else {
		for (cell = 0; cell < histogram_cells; cell++)
			histogram[cell] = ntohl(histogram[cell]);
	}
out:
	sem_post(&logger_done);

	return NULL;
}

static void setup_measurement_on_gpio(void)
{
	struct latmon_net_request req;
	struct sockaddr_in in_addr;
	bool latmon_hung = false;
	struct timespec timeout;
	int ret, sig;

	lat_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (lat_sock < 0)
		error(1, errno, "socket()");

	if (verbosity)
		printf("connecting to latmon at %s:%d...\n",
			inet_ntoa(gpio_monitor_ip), LATMON_NET_PORT);

	memset(&in_addr, 0, sizeof(in_addr));
	in_addr.sin_family = AF_INET;
	in_addr.sin_addr = gpio_monitor_ip;
	in_addr.sin_port = htons(LATMON_NET_PORT);
	ret = connect(lat_sock, (struct sockaddr *)&in_addr,
		sizeof(in_addr));
	if (ret)
		error(1, errno, "connect()");

	if (verbosity && context_type == EVL_LAT_INBAND_GPIO)
		printf("CAUTION: measuring in-band response time (no EVL there)\n");

	create_responder(&responder, gpio_responder_thread);
	create_logger(&logger, sock_logger_thread, &latmon_hung);

	req.period_usecs = htonl(period_usecs); /* Non-zero, means start. */
	req.histogram_cells = histogram ? htonl(histogram_cells) : 0;
	ret = send(lat_sock, &req, sizeof(req), 0);
	if (ret != sizeof(req))
		error(1, errno, "send() start");

	sigwait(&sigmask, &sig);

	/*
	 * From now on, we may wait up to LATMON_TIMEOUT_SECS
	 * max. between messages from the remote latency monitor
	 * before declaring it unresponsive.
	 */
	if (!latmon_hung) {
		req.period_usecs = 0; /* Zero means stop. */
		req.histogram_cells = 0;
		ret = send(lat_sock, &req, sizeof(req), 0);
		if (ret != sizeof(req))
			error(1, errno, "send() start");
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += LATMON_TIMEOUT_SECS;
		if (sem_timedwait(&logger_done, &timeout))
			latmon_hung = true;
	}

	if (latmon_hung)
		error(1, ETIMEDOUT, "latmon at %s is unresponsive",
			inet_ntoa(gpio_monitor_ip));

	close(lat_sock);
}

static void paste_file_in(const char *path, const char *header)
{
	char buf[BUFSIZ];
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return;

	fprintf(plot_fp, "# %s", header ?: "");

	while (fgets(buf, sizeof(buf), fp))
		fputs(buf, plot_fp);

	fclose(fp);
}

static void dump_gnuplot(time_t duration)
{
	int first, last, n;

	if (all_samples == 0)
		return;

	fprintf(plot_fp, "# test started on: %s", ctime(&start_time));
	paste_file_in("/proc/version", NULL);
	paste_file_in("/proc/cmdline", NULL);
	fprintf(plot_fp, "# libevl version: %s\n", evl_get_version().version_string);
	fprintf(plot_fp, "# sampling period: %u microseconds\n", period_usecs);
	paste_file_in("/sys/devices/virtual/clock/monotonic/gravity",
		"clock gravity: ");
	paste_file_in("/sys/devices/system/clocksource/clocksource0/current_clocksource",
		"clocksource: ");
	paste_file_in("/sys/devices/system/clocksource/clocksource0/vdso_clocksource",
		"vDSO access: ");
	fprintf(plot_fp, "# context: %s\n", context_labels[context_type]);
	if (!test_irqlat) {
		fprintf(plot_fp, "# thread priority: %d\n", responder_priority);
		fprintf(plot_fp, "# thread affinity: CPU%d%s\n",
			responder_cpu,
			responder_cpu_state & EVL_CPU_ISOL ? "" : "-noisol");
	}
	if (c_state_restricted)
		fprintf(plot_fp, "# C-state restricted\n");
	fprintf(plot_fp, "# duration (hhmmss): %.2ld:%.2ld:%.2ld\n",
		duration / 3600, (duration / 60) % 60, duration % 60);
	fprintf(plot_fp, "# peak (hhmmss): %.2ld:%.2ld:%.2ld\n",
		peak_time / 3600, (peak_time / 60) % 60, peak_time % 60);
	if (all_overruns > 0)
		fprintf(plot_fp, "# OVERRUNS: %u\n", all_overruns);
	if (spurious_inband_switches > 0)
		fprintf(plot_fp, "# IN-BAND SWITCHES: %u\n", spurious_inband_switches);
	fprintf(plot_fp, "# min latency: %.3f\n",
		(double)all_minlat / 1000.0);
	fprintf(plot_fp, "# avg latency: %.3f\n",
		(double)(all_sum / all_samples) / 1000.0);
	fprintf(plot_fp, "# max latency: %.3f\n",
		(double)all_maxlat / 1000.0);

	for (n = 0; n < histogram_cells && histogram[n] == 0; n++)
		;
	first = n;

	for (n = histogram_cells - 1; n >= 0 && histogram[n] == 0; n--)
		;
	last = n;

	for (n = first; n < last; n++)
		/* +1: no zero value for intermediate cells. */
		fprintf(plot_fp, "%d %d\n", n, histogram[n] + 1);

	/*
	 * If we have outliers, display a '+' sign after the last cell
	 * index.
	 */
	fprintf(plot_fp, "%d%s %d\n", last,
		all_maxlat / 1000 >= histogram_cells ? "+" : "",
		histogram[last]);
}

static void do_measurement(int type)
{
	const char *cpu_s = "";
	time_t duration;

	context_type = type;

	if (plot_fp) {
		histogram = malloc(histogram_cells * sizeof(int32_t));
		if (histogram == NULL)
			error(1, ENOMEM, "cannot get memory");
	}

	if (!(responder_cpu_state & EVL_CPU_ISOL))
		cpu_s = " (not isolated)";

	if (verbosity > 0)
		fprintf(stderr, "warming up on CPU%d%s...\n", responder_cpu, cpu_s);
	else
		fprintf(stderr, "running quietly for %ld seconds on CPU%d%s\n",
			timeout, responder_cpu, cpu_s);

	switch (type) {
	case EVL_LAT_OOB_GPIO:
	case EVL_LAT_INBAND_GPIO:
		setup_measurement_on_gpio();
		break;
	default:
		setup_measurement_on_timer();
	}

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
			all_overruns, spurious_inband_switches,
			duration / 3600, (duration / 60) % 60,
			duration % 60, duration / 3600,
			(timeout / 60) % 60, timeout % 60);

	if (spurious_inband_switches > 0) {
		if (all_samples > 0)
			fputc('\n', stderr);
		fprintf(stderr, "*** WARNING: unexpected switches to in-band mode detected,\n"
		       "             latency figures displayed are NOT reliable.\n"
		       "             Please submit a bug report upstream.\n");
		if (abort_on_switch) {
			abort_on_switch = false;
			fprintf(stderr, "*** OOPS: aborting upon spurious switch to in-band mode.\n");
		}
	}

	if (responder)
		pthread_cancel(responder);

	if (logger)
		pthread_cancel(logger);
}

static void do_tuning(int type)
{
	struct latmus_result result;
	struct latmus_setup setup;
	pthread_t responder;
	__s32 gravity;
	int ret;

	if (verbosity) {
		printf("%s gravity...", context_labels[type]);
		fflush(stdout);
	}

	memset(&setup, 0, sizeof(setup));
	setup.type = type;
	setup.period = period_usecs * 1000ULL; /* ns */
	setup.priority = responder_priority;
	setup.cpu = responder_cpu;
	setup.u.tune.verbosity = verbosity;
	ret = ioctl(latmus_fd, EVL_LATIOC_TUNE, &setup);
	if (ret)
		error(1, errno, "tuning setup failed (%s)", context_labels[type]);

	if (type == EVL_LAT_USER)
		create_responder(&responder, timer_responder);

	pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);

	result.data_ptr = (__u64)(uintptr_t)&gravity;
	result.len = sizeof(gravity);
	ret = oob_ioctl(latmus_fd, EVL_LATIOC_RUN, &result);
	if (ret)
		error(1, errno, "measurement failed");

	if (type == EVL_LAT_USER)
		pthread_cancel(responder);

	if (verbosity)
		printf("%u ns\n", gravity);
}

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	if (sigdebug_marked(si)) {
		switch (sigdebug_cause(si)) {
		case EVL_HMDIAG_SIGDEMOTE:
		case EVL_HMDIAG_SYSDEMOTE:
		case EVL_HMDIAG_EXDEMOTE:
		case EVL_HMDIAG_LKDEPEND:
			spurious_inband_switches++;
			if (abort_on_switch)
				kill(getpid(), SIGHUP);
			break;
		case EVL_HMDIAG_WATCHDOG:
		case EVL_HMDIAG_LKIMBALANCE:
		case EVL_HMDIAG_LKSLEEP:
		default:
			exit(99);
		}
	}
}

static void set_cpu_affinity(void)
{
	cpu_set_t cpu_set;
	int ret;

	CPU_ZERO(&cpu_set);
	CPU_SET(responder_cpu, &cpu_set);
	ret = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	if (ret)
		error(1, errno, "cannot set affinity to CPU%d",
		      responder_cpu);
}

static void restrict_c_state(void)
{
	__s32 val = 0;
	int fd;

	fd = open("/dev/cpu_dma_latency", O_WRONLY);
	if (fd < 0)
		return;

	if (write(fd, &val, sizeof(val) == sizeof(val)))
		c_state_restricted = true;
}

static void parse_host_spec(const char *host, struct in_addr *in_addr)
{
	struct addrinfo hints, *res;
	int ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG;
	ret = getaddrinfo(host, NULL, &hints, &res);
	if (ret)
		error(1, ret == EAI_SYSTEM ? errno : ESRCH,
			"getaddrinfo(%s)", host);

	*in_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
}

static int parse_gpio_spec(const char *spec, int *pin,
			int *hdflags, int *evflags)
{
	char *s, *p, *endptr, *devname;
	int fd, ret;

	s = strdup(spec);
	p = strtok(s, ",");
	if (p == NULL)
		error(1, EINVAL, "no GPIO device in spec: %s", spec);

	ret = asprintf(&devname, "/dev/%s", p);
	if (ret < 0)
		error(1, ENOMEM, "asprintf()");

	p = strtok(NULL, ",");
	if (p == NULL)
		error(1, EINVAL, "no GPIO pin in spec: %s", spec);

	*pin = (int)strtol(p, &endptr, 10);
	if (*pin < 0 || endptr == p)
		error(1, EINVAL, "invalid GPIO pin number in spec: %s",
			spec);

	p = strtok(NULL, ",");
	if (evflags) {
		if (p) {
			if (!strcmp(p, "rising-edge"))
				*evflags = GPIOEVENT_REQUEST_RISING_EDGE;
			else if  (!strcmp(p, "falling-edge"))
				*evflags = GPIOEVENT_REQUEST_FALLING_EDGE;
			else
				error(1, EINVAL, "invalid edge type in spec: %s",
					spec);
		} else	/* Default is rising edge. */
			*evflags = GPIOEVENT_REQUEST_RISING_EDGE;
	} else if (p)
		error(1, EINVAL, "trailing garbage in spec: %s",
			spec);

	fd = open(devname, O_RDONLY);
	if (fd < 0)
		error(1, errno, "open(%s)", devname);

	free(devname);
	free(s);

	return fd;
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

static void determine_responder_cpu(bool inband_test)
{
	cpu_set_t oob_cpus, best_cpus;
	int cpu;

	parse_cpu_list(ISOLATED_CPU_LIST, &isolated_cpus);
	parse_cpu_list(OOB_CPU_LIST, &oob_cpus);

	if (responder_cpu >= 0) {
		if (!inband_test && !CPU_ISSET(responder_cpu, &oob_cpus)) {
			if (verbosity)
				printf("CPU%d is not OOB-capable, "
					"picking a better one\n",
					responder_cpu);
			goto pick_oob;
		}
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
			responder_cpu = cpu;
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
			responder_cpu = cpu;
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
			responder_cpu = cpu;
			goto finish;
		}
	}

	/* Out of luck, run on the current CPU. */
	if (responder_cpu < 0)
		responder_cpu = sched_getcpu();
finish:
	if (CPU_ISSET(responder_cpu, &isolated_cpus))
		responder_cpu_state = EVL_CPU_ISOL;
}

static void usage(void)
{
        fprintf(stderr, "usage: latmus [options]:\n");
        fprintf(stderr, "-m --measure            measure latency on timer event [default]\n");
        fprintf(stderr, "-t --tune               tune the EVL core timer\n");
        fprintf(stderr, "-i --irq                measure/tune interrupt latency\n");
        fprintf(stderr, "-k --kernel             measure/tune kernel scheduling latency\n");
        fprintf(stderr, "-u --user               measure/tune user scheduling latency\n");
        fprintf(stderr, "    [ if none of --irq, --kernel or --user is given,\n"
                        "      tune for all contexts ]\n");
        fprintf(stderr, "-p --period=<us>        sampling period\n");
        fprintf(stderr, "-P --priority=<prio>    responder thread priority [=90]\n");
        fprintf(stderr, "-c --cpu=<n>            pin responder thread to CPU [=0]\n");
        fprintf(stderr, "-r --reset              reset core timer gravity to factory default\n");
        fprintf(stderr, "-b --background         run in the background (daemon mode)\n");
        fprintf(stderr, "-K --keep-going         keep going on unexpected switch to in-band mode\n");
        fprintf(stderr, "-A --max-abort=<us>     abort if maximum latency exceeds threshold\n");
        fprintf(stderr, "-T --timeout=<t>[dhms]  stop measurement after <t> [d(ays)|h(ours)|m(inutes)|s(econds)]\n");
        fprintf(stderr, "-v --verbose[=level]    set verbosity level [=1]\n");
        fprintf(stderr, "-q --quiet              quiet mode (i.e. --verbose=0)\n");
        fprintf(stderr, "-l --lines=<num>        result lines per page, 0 = no pagination [=21]\n");
        fprintf(stderr, "-H --histogram[=<nr>]   set histogram size to <nr> cells [=200]\n");
        fprintf(stderr, "-g --plot=<filename>    dump histogram data to file (gnuplot format)\n");
        fprintf(stderr, "-Z --oob-gpio=<host>    measure EVL response time to GPIO event via <host>\n");
        fprintf(stderr, "-z --inband-gpio=<host> measure inband response time to GPIO event via <host>\n");
        fprintf(stderr, "-I --gpio-in=<spec>     input GPIO line configuration\n");
        fprintf(stderr, "   with <spec> = gpiochip-devname,pin-number[,rising-edge|falling-edge]\n");
        fprintf(stderr, "-O --gpio-out=<spec>    output GPIO line configuration\n");
        fprintf(stderr, "   with <spec> = gpiochip-devname,pin-number\n");
}

static void bad_usage(int argc, char *const argv[])
{
	int last = optind < argc ? optind : argc - 1;
	printf("** Uh, you lost me somewhere near '%s' (arg #%d)\n", argv[last], last);
	usage();
}

int main(int argc, char *const argv[])
{
	int ret, c, spec, type, max_prio, lindex;
	const char *plot_filename = NULL;
	struct sigaction sa;
	char *endptr;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, &lindex);
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
			reset = true;
			break;
		case 'q':
			verbosity = 0;
			break;
		case 'b':
			background = true;
			break;
		case 'K':
			abort_on_switch = false;
			break;
		case 'm':
			tuning = 0;
			break;
		case 't':
			tuning = 1;
			break;
		case 'p':
			period_usecs = atoi(optarg);
			if (period_usecs <= 0 || period_usecs > 1000000)
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
				error(1, EINVAL, "invalid time modifier: '%c'",
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
			responder_priority = atoi(optarg);
			if (responder_priority < 0 || responder_priority > max_prio)
				error(1, EINVAL, "invalid thread priority "
				      "(0 < priority < %d)", max_prio);
			break;
		case 'c':
			responder_cpu = atoi(optarg);
			if (responder_cpu < 0 || responder_cpu >= CPU_SETSIZE)
				error(1, EINVAL, "invalid CPU number");
			break;
		case 'z':
		case 'Z':
			test_gpiolat = (c == 'z') + 1;
			parse_host_spec(optarg, &gpio_monitor_ip);
			break;
		case 'I':
			gpio_infd = parse_gpio_spec(optarg, &gpio_inpin,
					&gpio_hdinflags, &gpio_evinflags);
			break;
		case 'O':
			gpio_outfd = parse_gpio_spec(optarg, &gpio_outpin,
					&gpio_hdoutflags, NULL);
			break;
		case '?':
		default:
			bad_usage(argc, argv);
			return 1;
		}
	}

	if (optind < argc) {
		bad_usage(argc, argv);
		return 1;
	}

	determine_responder_cpu(test_gpiolat == INBAND_GPIO_LAT);

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

	if (background) {
		signal(SIGHUP, SIG_IGN);
		ret = daemon(0, 0);
		if (ret)
			error(1, errno, "cannot daemonize");
	}

	set_cpu_affinity();
	restrict_c_state();

	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGHUP);
	sigaddset(&sigmask, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction(SIGDEBUG, &sa, NULL);

	spec = test_irqlat || test_klat || test_ulat || test_gpiolat;
	if (!tuning) {
		if (!spec)
			test_ulat = 1;
		else if (test_irqlat + test_klat + test_ulat + (!!test_gpiolat) > 1)
			error(1, EINVAL, "only one of -u, -k, -i, -z or -Z "
			      "in measurement mode");
	} else {
		/* Default to tune for all contexts. */
		if (!spec)
			test_irqlat = test_klat = test_ulat = 1;
		else if (test_gpiolat)
			error(1, EINVAL, "-z and -t are mutually exclusive");
	}

	if (test_gpiolat != INBAND_GPIO_LAT) {
		ret = evl_init();
		if (ret)
			error(1, -ret, "evl_init()");
	}

	if (!test_gpiolat) {
		latmus_fd = open("/dev/latmus", O_RDWR);
		if (latmus_fd < 0)
			error(1, errno, "cannot open latmus device");

		if (reset) {
			ret = ioctl(latmus_fd, EVL_LATIOC_RESET);
			if (ret)
				error(1, errno, "reset failed");
		}
	} else {
		if (gpio_infd < 0 || gpio_outfd < 0)
			error(1, EINVAL, "-[zZ] require -I, -O for GPIO settings");
		if (test_gpiolat == OOB_GPIO_LAT) {
			gpio_hdinflags |= GPIOHANDLE_REQUEST_OOB;
			gpio_hdoutflags |= GPIOHANDLE_REQUEST_OOB;
		}
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
			EVL_LAT_KERN : EVL_LAT_USER + test_gpiolat;
		do_measurement(type);
	} else {
		if (verbosity)
			printf("== latmus started for core tuning, "
			       "period=%d microseconds (may take a while)\n",
			       period_usecs);

		ret = evl_attach_self("/clock-tuner:%d", getpid());
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

	return 0;
}
