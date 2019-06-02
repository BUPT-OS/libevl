/*
 * SPDX-License-Identifier: MIT
 */

/*
 * Usage:
 *      example to monitor GPIO 23 and respond on GPIO 24 using EVL to
 *      monitor and respond:
 *
 *      $ gpio-echo -n gpiochip0 -o 23 -t 24 -O -T -f
 *
 * To be used in collaboration with an external test tool
 *	https://github.com/ldts/zephyr/commit/b157dc635196ce501e0cf2f70ae1bd5fe3f6a300
 *
 */
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <evl/thread.h>
#include <evl/mutex.h>
#include <evl/clock.h>
#include <evl/sem.h>
#include <evl/proxy.h>
#include <linux/gpio.h>
#include <uapi/evl/devices/gpio.h>

#define VERBOSE 0

#if 0
#define gpio_printf(file, __fmt, __args...) fprintf(file, __fmt, ##__args)
#else
#define gpio_printf(file, __fmt, __args...) evl_printf(__fmt, ##__args)
#endif

static int gpio_tx_value(const int fd, struct gpiohandle_data *data, int oob)
{
	int ret;

	if (oob & GPIOHANDLE_REQUEST_OOB )
		ret = oob_ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, data);
	else
		ret = ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, data);

	if (ret == -1) {
		ret = -errno;
		gpio_printf(stderr, "Failed to issue %s (%d), %s\n",
			"GPIOHANDLE_SET_LINE_VALUES_IOCTL", ret,
			strerror(errno));
	}

	return ret;
}

static int echo_device(const char *device_name,
		   unsigned int line_in,
		   uint32_t handleflags_in,
		   uint32_t eventflags,
		   unsigned int loops,
         	   unsigned int line_out,
		   uint32_t handleflags_out)
{
	struct gpiohandle_request req_out = { 0 };
	struct gpiohandle_data data_out = { 0 };
        struct gpioevent_request req_in = { 0 };
	struct gpiohandle_data data_in = { 0 };
	char *chrdev_name;
	int fd;
	int ret;
	int i = 0;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	fd = open(chrdev_name, 0);
	if (fd == -1) {
		ret = -errno;
		gpio_printf(stderr, "Failed to open %s\n", chrdev_name);
		goto exit_close_error;
	}

        /* IN configuration */
	req_in.handleflags = handleflags_in;
	req_in.eventflags = eventflags;
	req_in.lineoffset = line_in;
	strcpy(req_in.consumer_label, "gpio-event-mon");

	ret = ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, &req_in);
	if (ret == -1) {
		ret = -errno;
		gpio_printf(stderr, "Failed to issue GET EVENT "
			"IOCTL (%d)\n",
			ret);
		goto exit_close_error;
	}

	/* Read initial states */
	ret = ioctl(req_in.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data_in);
	if (ret == -1) {
		ret = -errno;
		gpio_printf(stderr, "Failed to issue GPIOHANDLE GET LINE "
			"VALUES IOCTL (%d)\n",
			ret);
		goto exit_close_error;
	}

	gpio_printf(stdout, "RX line %d on %s\n", line_in, device_name);
	gpio_printf(stdout, " initial value: %d\n", data_in.values[0]);
	if (handleflags_in & GPIOHANDLE_REQUEST_OOB)
		gpio_printf(stdout, " out-of-band rx enabled\n");

        /* OUT configuration */
	req_out.lineoffsets[0] = line_out;
        req_out.lines = 1;
	req_out.flags = handleflags_out;
        req_out.default_values[0] = 1;
	strcpy(req_out.consumer_label, "gpio-event-echo");

	ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req_out);
	if (ret == -1) {
		ret = -errno;
		gpio_printf(stderr, "Failed to issue %s (%d), %s\n",
			"GPIO_GET_LINEHANDLE_IOCTL", ret, strerror(errno));
                gpio_printf(stderr, "Issue GPIO_GET_LINEHANDLE_IOCTL \n"
                        "req.lines %d, req.flags 0x%x req.lineoffsets[0] = %d, \n",
		req_out.lines, req_out.flags, req_out.lineoffsets[0]);
		goto exit_close_error;
	}

        gpio_printf(stdout, "TX line %d on %s\n", line_out, device_name);
        gpio_printf(stdout, " initial value: %d\n", data_out.values[0]);
        if (handleflags_out & GPIOHANDLE_REQUEST_OOB)
                gpio_printf(stdout, " out-of-band tx enabled\n");

	while (1) {
		struct gpioevent_data event;

                data_out.values[0] = 0;
                ret = gpio_tx_value(req_out.fd, &data_out, handleflags_out);
                if (ret == -1) {
                        ret = -errno;
                        gpio_printf(stderr, "Failed to issue %s (%d), %s\n",
                                "GPIO_GET_LINEHANDLE_IOCTL", ret, strerror(errno));

                        goto release_out_handler;
                }

		if (handleflags_in & GPIOHANDLE_REQUEST_OOB)
			ret = oob_read(req_in.fd, &event, sizeof(event));
		else
			ret = read(req_in.fd, &event, sizeof(event));

                if (ret == -1) {
			if (errno == -EAGAIN) {
				gpio_printf(stderr, "nothing available\n");
				continue;
			} else {
				ret = -errno;
				gpio_printf(stderr, "Failed to read event (%d)\n",
					ret);
				break;
			}
		}

		if (ret != sizeof(event)) {
			gpio_printf(stderr, "Reading event failed\n");
			ret = -EIO;
			break;
		}

#if VERBOSE
		gpio_printf(stdout, "\tGPIO EVENT[%04d] = %llu: ", i, event.timestamp);
#endif

                switch (event.id) {
		case GPIOEVENT_EVENT_RISING_EDGE:
#if VERBOSE
			gpio_printf(stdout, "rising edge");
#endif
			break;
		case GPIOEVENT_EVENT_FALLING_EDGE:
#if VERBOSE
			gpio_printf(stdout, "falling edge");
#endif

                        /* respond to sender with rising edge */
                        data_out.values[0] = 1;
                        ret = gpio_tx_value(req_out.fd, &data_out, handleflags_out);
                        if (ret == -1)
                                goto release_out_handler;
			break;
		default:
			gpio_printf(stdout, "unknown event");
		}
#if VERBOSE
		gpio_printf(stdout, "\n");
#endif

		i++;
		if (i == loops)
			break;
	}

release_out_handler:
        close(req_out.fd);

exit_close_error:
	if (close(fd) == -1)
		perror("Failed to close GPIO character device file");
	free(chrdev_name);
	return ret;
}

static void print_usage(void)
{
	gpio_printf(stderr, "Usage: gpio-event-mon [options]...\n"
		"Listen to events on GPIO lines, 0->1 1->0\n"
		"  -n <name>  Listen on GPIOs on a named device (must be stated)\n"
		"  -o <n>     Offset to monitor\n"
		"  -t <n>     Offset to respond\n"
		"  -d         Set line as open drain\n"
		"  -s         Set line as open source\n"
		"  -O         Enable out-of-band event handling\n"
		"  -T         Enable out-of-band event tx handling\n"
		"  -r         Listen for rising edges\n"
		"  -f         Listen for falling edges\n"
		" [-x <n>]    pin process to CPU [=1]\n"
		" [-c <n>]    Do <n> loops (optional, infinite loop if not stated)\n"
		"  -?         This helptext\n"
		"\n"
		"Example:\n"
		"gpio-event-mon -n gpiochip0 -o 4 -r -f\n"
	);
}

int main(int argc, char **argv)
{
	const char *device_name = NULL;
	unsigned int line_in = -1, line_out = -1;
	unsigned int loops = 0;
	uint32_t handleflags_in = GPIOHANDLE_REQUEST_INPUT;
	uint32_t handleflags_out = GPIOHANDLE_REQUEST_OUTPUT;
	uint32_t eventflags = 0;
	int pin_cpu = -1;
	struct sched_param param;
	cpu_set_t cpu_set;
	int c;
	int ret;

	while ((c = getopt(argc, argv, "c:n:o:t:x:dsOTrf?")) != -1) {
		switch (c) {
		case 'c':
			loops = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			device_name = optarg;
			break;
		case 'o':
			line_in = strtoul(optarg, NULL, 10);
			break;
		case 't':
			line_out = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			handleflags_in |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
			break;
		case 's':
			handleflags_in |= GPIOHANDLE_REQUEST_OPEN_SOURCE;
			break;
		case 'O':
			handleflags_in |= GPIOHANDLE_REQUEST_OOB;
			break;
		case 'T':
			handleflags_out |= GPIOHANDLE_REQUEST_OOB;
			break;
		case 'r':
			eventflags |= GPIOEVENT_REQUEST_RISING_EDGE;
			break;
		case 'f':
			eventflags |= GPIOEVENT_REQUEST_FALLING_EDGE;
			break;
		case 'x':
			pin_cpu = atoi(optarg);
			if (pin_cpu < 1 || pin_cpu >= CPU_SETSIZE) {
				printf("Invalid CPU number %d\n", pin_cpu);
				print_usage();
				return -1;
			}
			break;
		case '?':
			print_usage();
			return -1;
		}
	}

	if (!device_name || line_in == -1 || line_out == -1) {
		print_usage();
		return -1;
	}
	if (!eventflags) {
		printf("No flags specified, listening on both rising and "
		       "falling edges\n");
		eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
	}

	if (pin_cpu > 0) {
		CPU_ZERO(&cpu_set);
		CPU_SET(pin_cpu, &cpu_set);
		ret = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
		if (ret)
			error(1, errno, "cannot set affinity to CPU%d",
				pin_cpu);
		else
			gpio_printf(stderr, "Affinity on CPU%d\n", pin_cpu);
	} else
		gpio_printf(stderr, "No cpu Affinity\n");

	param.sched_priority = 98;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

	evl_attach_self("gpio-mon:%d", getpid());

	return echo_device(device_name, line_in, handleflags_in,
			      eventflags, loops, line_out, handleflags_out);
}
