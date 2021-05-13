/*
 * SPDX-License-Identifier: MIT
 *
 * This tidbit demonstrates out-of-band SPI transfers controlled from
 * user-space, using the extension the EVL core adds to the common
 * SPIDEV interface. Typical use case: closed-loop control systems
 * having stringent requirements, i.e. high frequency and/or low
 * jitter.  The magic starts with the ioctl(SPI_IOC_ENABLE_OOB_MODE)
 * request, check out the code below.
 *
 * Using a Raspberry PI2/3/4, you can easily demo this code with a
 * simple loopback test, by shorting PIN #19 (SPI_MOSI) and #21
 * (SPI_MISO) on the GPIO 40 pin header, using the SPI0 controller
 * (bcm2835). You need the following features to be enabled in your PI
 * kernel configuration:
 *
 * - CONFIG_SPI=[y|m]
 * - CONFIG_SPI_OOB=y
 * - CONFIG_SPI_SPIDEV=[y|m]
 * - CONFIG_SPI_SPIDEV_OOB=y
 * - CONFIG_DMA_BCM2835=[y|m]
 * - CONFIG_DMA_BCM2835_OOB=y
 *
 * Usage:
 * ~# oob-spi [/dev/spidevX.Y]
 *
 * What's so interesting with this test? Check the delay reported for
 * the transfer, with and without load. Typically, you could run the
 * following stress load in the background:
 *
 * ~# dd if=/dev/zero of=/dev/null bs=128M &
 * ~# while :; do hackbench; done&
 *
 * TROUBLESHOOTING:
 *
 * - if you get -ENODEV on return to ioctl(SPI_IOC_ENABLE_OOB_MODE),
 * then you should suspect the DMA settings for the SPI
 * controller. DMA support is required for out-of-band transfers.
 *
 * - if you have no /dev/spidev* devices listed although the proper
 * SPI driver is enabled in the kernel configuration, then the device
 * tree for the SoC may be missing a SPI(dev) slave device declaration
 * in the proper SPI bus stanza. You may want to set the compatible
 * string to "spidev,loopback", which is detected by the EVL-enabled
 * spidev driver.
 *
 * spi0 {
 *      ...
 * 	spidev_0: spidev@0{
 * 	       compatible = "spidev,loopback";
 * 	       reg = <0>;
 * 	       #address-cells = <1>;
 * 	       #size-cells = <0>;
 * 	       spi-max-frequency = <12500000>;
 * 	       status = "okay";
 * 	};
 * };
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <evl/thread.h>
#include <evl/clock.h>
#include <evl/proxy.h>
#include <linux/spi/spidev.h>
#include <uapi/evl/devices/spidev.h>

static const char *device = "/dev/spidev0.0";
static uint32_t mode = SPI_MODE_0;
static uint8_t bits = 8;
static uint32_t speed = 2500000;
static int len = 140;

static void timespec_sub(struct timespec *__restrict r,
		  const struct timespec *__restrict t1,
		  const struct timespec *__restrict t2)
{
	r->tv_sec = t1->tv_sec - t2->tv_sec;
	r->tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

int main(int argc, char *argv[])
{
	struct spi_ioc_oob_setup oob_setup;
	struct timespec begin, end, delta;
	struct sched_param param;
	int tfd, devfd, ret, n;
	char *tx, *rx;
	void *iobuf;

	if (argc > 1)
		device = argv[1];

	/*
	 * This is usual SPI configuration stuff using the spidev
	 * interface.
	 */
	devfd = open(device, O_RDWR);
	if (devfd < 0)
		error(1, errno, "can't open device %s", device);

	ret = ioctl(devfd, SPI_IOC_WR_MODE32, &mode);
	if (ret)
		error(1, errno, "ioctl(SPI_IOC_WR_MODE32)");

	ret = ioctl(devfd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret)
		error(1, errno, "ioctl(SPI_IOC_WR_BITS_PER_WORD)");

	ret = ioctl(devfd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret)
		error(1, errno, "ioctl(SPI_IOC_WR_MAX_SPEED_HZ)");

	/*
	 * This part switches the device to out-of-band operation
	 * mode. In this case, I/O is performed via the DMA engine
	 * exclusively, directly from/to buffers (m)mapped into the
	 * address space of the client.
	 */
	oob_setup.frame_len = len;
	oob_setup.speed_hz = speed;
	oob_setup.bits_per_word = bits;
	ret = ioctl(devfd, SPI_IOC_ENABLE_OOB_MODE, &oob_setup);
	if (ret)
		error(1, errno, "ioctl(SPI_IOC_ENABLE_OOB_MODE)");

	printf("mapping %d bytes, tx@%d, rx@%d, frame_len=%d\n",
		oob_setup.iobuf_len, oob_setup.tx_offset, oob_setup.rx_offset, len);

	/*
	 * We may map the I/O area now, it is composed of two adjacent
	 * buffers of @len bytes (plus alignment). CAUTION: the
	 * mapping is always on coherent DMA memory (i.e. non-cached).
	 */
	iobuf = mmap(NULL, oob_setup.iobuf_len, PROT_READ|PROT_WRITE,
		MAP_SHARED, devfd, 0);
	if (iobuf == MAP_FAILED)
		error(1, errno, "mmap()");

	/*
	 * Trick: we want evl_attach_self() to inherit the SCHED_FIFO
	 * policy and priority for scheduling by the EVL core. We
	 * could have used evl_set_schedattr() explicitly once
	 * attached instead.
	 */
	param.sched_priority = 1;
	ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	if (ret)
		error(1, ret, "pthread_setschedparam()");

	/*
	 * The core told us where to read and write data on return to
	 * ioctl(SPI_IOC_ENABLE_OOB_MODE), which is at some offset
	 * from the I/O buffer we received from mmap().
	 */
	tx = iobuf + oob_setup.tx_offset;
	memset(tx, 0x42, len);
	rx = iobuf + oob_setup.rx_offset;
	memset(rx, 0, len);

	/* Let's attach to the EVL core. */
	tfd = evl_attach_self("oob-spi:%d", getpid());
	if (tfd < 0)
		error(1, -tfd, "cannot attach to the EVL core");

	/*
	 * The actual I/O request: sending from @tx, receiving to
	 * @rx. Do some trivial test, running a single
	 * transaction. You may want to try making this a loop.
	 */
	evl_read_clock(EVL_CLOCK_MONOTONIC, &begin);

	ret = oob_ioctl(devfd, SPI_IOC_RUN_OOB_XFER);
	if (ret)
		error(1, errno, "oob_ioctl(SPI_IOC_RUN_OOB_XFER)");

	evl_read_clock(EVL_CLOCK_MONOTONIC, &end);

	/*
	 * Do some visual control of the input buffer we received, it
	 * should be filled with value 0x42. We could have used
	 * printf(3) in the dump loop below, but were you to include
	 * this in an out-of-band loop for obtaining latency figures,
	 * then you would want evl_printf() to handle the printouts,
	 * so that no delay is incurred.
	 */
	timespec_sub(&delta, &end, &begin);
	evl_printf("transfer done in %ld s, %ld us:",
		delta.tv_sec, delta.tv_nsec / 1000);

	/* Dump the contents of the input buffer. */
	for (n = 0; n < len; n++) {
		if (!(n % 16))
			evl_printf("\n");
		evl_printf("%.2x ", rx[n]);
	}

	evl_printf("\n");

	/* All done, wrap it up. */

	munmap(iobuf, oob_setup.iobuf_len);

	ret = ioctl(devfd, SPI_IOC_DISABLE_OOB_MODE);
	if (ret)
		error(1, errno, "ioctl(SPI_IOC_DISABLE_OOB_MODE)");

	close(devfd);

	return 0;
}
