/*
 * Copyright (c) 2020 Philippe Gerum <rpm@xenomai.org>
 *    - with DHCP bits pulled from samples/net/dhcpv4_client/src/
 *    Copyright (c) 2017 ARM Ltd.
 *    Copyright (c) 2016 Intel Corporation.
 *    - with GPIO setup bits pulled from the gpio_latency driver
 *    Copyright (c) 2019 Jorge Ramirez-Ortiz <jro@xenomai.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(latency_monitor, LOG_LEVEL_DBG);

#include <linker/sections.h>
#include <errno.h>
#include <stdio.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/socket.h>
#include <gpio.h>
#include <zephyr.h>
#include "latmon.h"

static K_SEM_DEFINE(dhcp_done, 0, 1);

static K_SEM_DEFINE(monitor_done, 0, 1);

static struct in_addr local_ip;

#define MONITOR_STACK_SIZE 4096
#define MONITOR_THREAD_PRIORITY K_PRIO_COOP(8)
static K_THREAD_STACK_DEFINE(monitor_stack, MONITOR_STACK_SIZE);
static struct k_thread monitor_thread;
static k_tid_t monitor_tid;

static int client_socket;

static uint64_t sum_lat;

static uint32_t min_lat, max_lat;

static uint32_t overruns, current_samples;

static uint32_t max_samples_per_bulk;

static uint32_t period_usecs;

static uint32_t histogram_cells;

/*
 * Each cell represents a 1 usec timespan, the sampling period cannot
 * be longer than 1 sec.
 */
#define HISTOGRAM_CELLS_MAX 1000
static uint32_t histogram[HISTOGRAM_CELLS_MAX];

static struct device *gpiodev_pulse, *gpiodev_ack;

static bool abort_monitor;

static K_SEM_DEFINE(ack_event, 0, 1);

static K_MUTEX_DEFINE(stat_mutex);

static void dhcp_handler(struct net_mgmt_event_callback *cb,
			u32_t mgmt_event,
			struct net_if *iface)
{
	struct net_if_config *cf = &iface->config;
	int n;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD)
		return;

	for (n = 0; n < NET_IF_MAX_IPV4_ADDR; n++) {
		if (cf->ip.ipv4->unicast[n].addr_type != NET_ADDR_DHCP)
			continue;

		local_ip = cf->ip.ipv4->unicast[n].address.in_addr;
		k_sem_give(&dhcp_done);
		break;
	}
}

#define GPIO_PULSE_DEVICE	DT_ALIAS_LATMON_PULSE_GPIOS_CONTROLLER
#define GPIO_PULSE_PIN		DT_ALIAS_LATMON_PULSE_GPIOS_PIN
#define GPIO_ACK_DEVICE		DT_ALIAS_LATMON_ACK_GPIOS_CONTROLLER
#define GPIO_ACK_PIN		DT_ALIAS_LATMON_ACK_GPIOS_PIN

static void gpio_ack_handler(struct device *port,
			struct gpio_callback *cb, u32_t pins);

static bool setup_gpio_pins(void)
{
	static struct gpio_callback gpio_cb;
	int ret;

	gpiodev_pulse = device_get_binding(GPIO_PULSE_DEVICE);
	if (!gpiodev_pulse) {
		LOG_ERR("cannot find GPIO controller: %s",
			GPIO_PULSE_DEVICE);
		return false;
	}

	ret = gpio_pin_configure(gpiodev_pulse,
				GPIO_PULSE_PIN, GPIO_DIR_OUT);
	if (ret) {
		LOG_ERR("cannot configure output pin %s.%d\n",
			GPIO_PULSE_DEVICE, GPIO_PULSE_PIN);
		return false;
	}

	ret = gpio_pin_write(gpiodev_pulse, GPIO_PULSE_PIN, 1);
	if (ret) {
		LOG_ERR("gpio out error: set");
		return false;
	}

	gpiodev_ack = device_get_binding(GPIO_ACK_DEVICE);
	if (!gpiodev_ack) {
		LOG_ERR("cannot find GPIO controller: %s",
			GPIO_ACK_DEVICE);
		return false;
	}

	ret = gpio_pin_configure(gpiodev_ack, GPIO_ACK_PIN,
				 (GPIO_DIR_IN | GPIO_INT |
				  GPIO_INT_EDGE | GPIO_INT_ACTIVE_LOW));
	if (ret) {
		LOG_ERR("cannot configure input pin %s.%d\n",
			GPIO_ACK_DEVICE, GPIO_ACK_PIN);
		return false;
	}

	gpio_init_callback(&gpio_cb, gpio_ack_handler, BIT(GPIO_ACK_PIN));

	ret = gpio_add_callback(gpiodev_ack, &gpio_cb);
	if (ret) {
		LOG_ERR("cannot add GPIO input callback");
		return false;
	}

	ret = gpio_pin_enable_callback(gpiodev_ack, GPIO_ACK_PIN);
	if (ret) {
		LOG_ERR("cannot enable GPIO input callback");
		return false;
	}

	return true;
}

static void reset_sampling_counters(void)
{
	sum_lat = 0;
	min_lat = UINT_MAX;
	max_lat = 0;
	overruns = 0;
	current_samples = 0;
}

static ssize_t write_socket(const void *buf, size_t count)
{
	ssize_t n = 0, ret;

	do {
		ret = send(client_socket, (const char *)buf + n, count, 0);
		if (ret <= 0)
			return ret;
		n += ret;
		count -= ret;
	} while (count > 0);

	return n;
}

static bool send_sample_bulk(void)
{
	struct latmon_net_data ndata;
	int ret;

	ndata.sum_lat_hi = htonl(sum_lat >> 32);
	ndata.sum_lat_lo = htonl(sum_lat & 0xffffffff);
	ndata.min_lat = htonl(min_lat);
	ndata.max_lat = htonl(max_lat);
	ndata.overruns = htonl(overruns);
	ndata.samples = htonl(current_samples);
	ret = write_socket(&ndata, sizeof(ndata));
	if (ret <= 0)
		return false;

	reset_sampling_counters();

	return true;
}

static bool send_trailing_data(void)
{
	int cell, count;
	ssize_t ret;

	if (current_samples > 0)
		send_sample_bulk();

	/* Send an all-zero, termination bulk to the peer. */
	send_sample_bulk();

	/* Finally, send the histogram data in network byte order. */
	if (histogram_cells > 0) {
		for (cell = 0; cell < histogram_cells; cell++)
			histogram[cell] = htonl(histogram[cell]);
		count = histogram_cells * sizeof(histogram[0]);
		ret = write_socket(histogram, count);
		if (ret <= 0) {
			LOG_INF("failed sending histogram data (ret=%d, errno %d)",
				ret, errno);
			return false;
		}
	}

	return true;
}

/*
 * To discard redundant ACK events since the device under test might
 * lag a bit too long for devalidating our input signal.
 */
static bool ack_wait;

/* In raw ticks */
static u32_t ack_date;

static void gpio_ack_handler(struct device *port,
			struct gpio_callback *cb, u32_t pins)
{
	if (ack_wait) {
		ack_date = k_cycle_get_32();
		ack_wait = false;
		k_sem_give(&ack_event);
	}
}

static int monitor(void)
{
	u32_t pulse_date, delta, delta_ns, delta_usecs, warmup_count = 0;
	unsigned int key;
	int cell;

	LOG_INF("monitoring started");

	k_sem_reset(&ack_event);
	ack_wait = false;

	while (!abort_monitor) {
		if (period_usecs >= 1000)
			k_usleep(period_usecs);
		else
			k_busy_wait(period_usecs);

		/* Trigger the pulse, prepare for ACK receipt. */
		key = irq_lock();
		gpio_pin_write(gpiodev_pulse, GPIO_PULSE_PIN, 0);
		pulse_date = k_cycle_get_32();
		/* We would need a wmb here on SMP. */
		ack_wait = true;
		irq_unlock(key);
		/* Wait a bit then deassert the signal. */
		k_busy_wait(1);
		gpio_pin_write(gpiodev_pulse, GPIO_PULSE_PIN, 1);

		/* Wait for the device under test to ACK. */
		if (k_sem_take(&ack_event, K_SECONDS(1))) {
			ack_wait = false;
			overruns++;
			continue;
		}

		if (warmup_count < max_samples_per_bulk) {
			warmup_count++;
			continue;
		}

		delta = ack_date < pulse_date ? ~pulse_date + 1 + ack_date :
			ack_date - pulse_date;
		delta_ns = (u32_t)k_cyc_to_ns_floor64(delta);

		sum_lat += delta_ns;
		if (delta_ns < min_lat)
			min_lat = delta_ns;
		if (delta_ns > max_lat)
			max_lat = delta_ns;

		delta_usecs = delta_ns / 1000;

		if (histogram_cells > 0) {
			cell = delta_usecs;
			if (cell >= histogram_cells) /* Outlier. */
				cell = histogram_cells - 1;
			histogram[cell]++;
		}

		while (delta_usecs > period_usecs) {
			overruns++;
			delta_usecs -= period_usecs;
		}

		if (++current_samples >= max_samples_per_bulk &&
			!send_sample_bulk())
			break;
	}

	k_sem_give(&monitor_done);
	monitor_tid = NULL;
	LOG_INF("monitoring stopped");

	return 0;
}

static int start_monitoring(int s, struct latmon_net_request *req)
{
	client_socket = s;
	period_usecs = ntohl(req->period_usecs);
	max_samples_per_bulk = 1000000 / period_usecs;
	if (period_usecs == 0 || period_usecs > 1000000) {
		LOG_INF("invalid period received: %u usecs\n",
			period_usecs);
		return false;
	}

	histogram_cells = ntohl(req->histogram_cells);
	if (histogram_cells > HISTOGRAM_CELLS_MAX) {
		LOG_INF("invalid histogram size receive: %u > %u cells\n",
			histogram_cells, HISTOGRAM_CELLS_MAX);
		return false;
	}

	if (histogram_cells > 0)
		memset(histogram, 0, sizeof(histogram));

	reset_sampling_counters();
	k_sem_reset(&monitor_done);
	abort_monitor = false;

	monitor_tid = k_thread_create(&monitor_thread, monitor_stack,
				MONITOR_STACK_SIZE, (k_thread_entry_t)monitor,
				NULL, NULL, NULL,
				MONITOR_THREAD_PRIORITY, 0, K_NO_WAIT);

	return true;
}

static void stop_monitoring(void)
{
	if (monitor_tid) {
		abort_monitor = true;
		k_sem_take(&monitor_done, K_FOREVER);
	}
}

void main(void)
{
	struct sockaddr_in bind_addr, clnt_addr;
	struct net_mgmt_event_callback mgmt_cb;
	char ip_buf[NET_IPV4_ADDR_LEN];
	struct latmon_net_request req;
	struct net_if *iface;
	int s, cl, on = 1;
	socklen_t len;

	if (!setup_gpio_pins())
		exit(1);

	LOG_INF("DHCPv4 binding...");

	net_mgmt_init_event_callback(&mgmt_cb, dhcp_handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	iface = net_if_get_default();

	for (;;) {
		net_dhcpv4_start(iface);
		if (!k_sem_take(&dhcp_done, K_SECONDS(5)))
			break;

		LOG_INF("no DHCP lease received yet, retrying...");
	}

	LOG_INF("DHCPv4 ok, listening on %s:%d",
		log_strdup(net_addr_ntop(AF_INET, &local_ip,
			ip_buf, sizeof(ip_buf))), LATMON_NET_PORT);

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0) {
		perror("socket");
		exit(1);
	}

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(LATMON_NET_PORT);
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if (bind(s, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		perror("bind");
		exit(1);
	}

	if (listen(s, 1) < 0) {
		perror("listen");
		exit(1);
	}

	for (;;) {
		len = sizeof(clnt_addr);
		LOG_INF("waiting for connection...");
		cl = accept(s, (struct sockaddr *)&clnt_addr, &len);
		if (cl < 0) {
			LOG_INF("failed accepting new connection?!");
			continue;
		}

		for (;;) {
			len = recv(cl, &req, sizeof(req), 0);
			stop_monitoring();
			if (len != sizeof(req))
				break;
			if (req.period_usecs) {
				if (!start_monitoring(cl, &req))
					break;
			} else {
				if (!send_trailing_data())
					break;
			}
		}

		close(cl);
	}
}
