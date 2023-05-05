/*
 * SPDX-License-Identifier: MIT
 *
 * This tidbit demonstrates out-of-band networking, by replying to
 * ICMPv4(ECHO) requests received from the raw packet socket
 * interface. Using the ping command is enough to trigger a response
 * from this program.
 *
 * ==
 * == EVL (IP) networking in a nutshell
 * ==
 *
 * EVL recognizes the IP packets it should handle based on VLAN
 * tagging. Packets which belong to a so-called 'out-of-band VLAN'
 * should go through the EVL stack, others should take the regular
 * delivery path through the common network stack.
 *
 * Would your ethernet switches have to be specifically 802.1Q-capable
 * in order to convey out-of-band traffic then? No. Dot1q has been
 * around for quite some time, so most switches should pass on frames
 * with ethertype set to the 802.1Q TPID "as is" to some port, and
 * they should also be able to cope with the four additional octets
 * involved in VLAN tagging without having to lower the MTU everywhere
 * (most equipments even support jumbo frames these days).
 *
 * ==
 * == Configuring the ICMPv4(ECHO) responder
 * ==
 *
 * Prep work: three steps are required before you can send/receive oob
 * network packets on the target system:
 *
 * 1. Create an out-of-band VLAN network device on the target
 * system. The next steps enables it as an out-of-band networking
 * port.
 *
 * 2. Turn the new VLAN device as an EVL network port, this can be
 * done either programmatically, or via /sysfs.  Once enabled, EVL
 * picks incoming packets received from the device by matching the
 * VLAN tag. Non-matching packets keep flowing through the common
 * network stack instead. Conversely, outgoing IP packets sent by the
 * EVL network stack via this device are VLAN-tagged accordingly.
 *
 * 3. Tell EVL to handle packets which belong to the VLAN you have
 * attached to, by updating the set of out-of-band VLANs it manages.
 *
 * Practically, enabling out-of-band networking for VLAN #42 on the
 * physical network interface named 'eth0' on the target system can
 * be done as follows:
 *
 * Attach a VLAN device with tag 42 to the real 'eth0' device
 * # ip link add link eth0 name eth0.42 type vlan id 42
 *
 * Assign an arbitrary address to that VLAN device, e.g. 10.10.10.11
 * # ip addr add 10.10.10.11/24 dev eth0.42
 *
 * Tell EVL that the VLAN device is an out-of-band networking port:
 * # echo 1 > /sys/class/net/eth0.42/oob_port
 *
 * Eventually, tell EVL to pick packets tagged for VLAN 42 (you could ask EVL
 * to monitor multiple VLANs by passing a list of tags like '42-45,100,107'
 * the same way):
 * # echo 42 > /sys/class/evl/control/vlans
 *
 * ==
 * == Configuring the ICMPv4(ECHO) issuer
 * ==
 *
 * The issuer system should run a ping command to the IP address of
 * the VLAN device created earlier for the responder. All you need is
 * create a peer VLAN device on some other host on the same LAN, then
 * ping the receiving machine which runs this (oob-net-icmp) program,
 * e.g. assuming 'eno2' is the name of the physical network
 * interface on such host:
 *
 * # sudo ip link add link eno2 name eno2.42 type vlan id 42
 * # sudo ip addr add 10.10.10.10/24 dev eno2.42
 * # sudo ip link set eno2.42 up
 * # ping 10.10.10.11
 *
 * Eventually, this test program running on the EVL-enabled machine
 * should output traces as it replies to the ICMPv4(ECHO) requests,
 * e.g.:
 *
 * # /usr/evl/tidbits/oob-net-icmp -i eth0.42
 * listening to interface eth0.42
 * [0] count=84, proto=0x800, ifindex=2, type=0, halen=6, mac=xx:xx:xx:xx:xx:xx
 * [1] count=84, proto=0x800, ifindex=2, type=0, halen=6, mac=xx:xx:xx:xx:xx:xx
 * [2] count=84, proto=0x800, ifindex=2, type=0, halen=6, mac=xx:xx:xx:xx:xx:xx
 * [3] count=84, proto=0x800, ifindex=2, type=0, halen=6, mac=xx:xx:xx:xx:xx:xx
 * [4] count=84, proto=0x800, ifindex=2, type=0, halen=6, mac=xx:xx:xx:xx:xx:xx
 * [5] count=84, proto=0x800, ifindex=2, type=0, halen=6, mac=xx:xx:xx:xx:xx:xx
 *
 * CAUTION: Some NICs (e.g. Intel e1000) may need a delay between the
 * moment the VLAN filter is updated and the link is enabled in their
 * hardware. If in doubt, make sure to pause for a short while between
 * both operations, especially if the corresponding 'ip' commands are
 * part of a shell script.
 */

#include <pthread.h>
#include <error.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <netinet/ip_icmp.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <evl/thread.h>
#include <evl/proxy.h>
#include <evl/net/socket.h>

static int verbosity = 1;

static uint16_t ip_checksum (void *buf, int len)
{
	uint16_t *p = buf;	/* buf is assumed to be 16bit-aligned. */
	uint32_t sum = 0;
	int count = len;

	while (count > 1) {
		sum += *p++;
		count -= sizeof(*p);
	}

	if (count > 0)
		sum += *(uint8_t *)p;

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum & 0xffff;
}

static void dump_packet(const char *title,
			const unsigned char *buf, int len)
{
	int n = 0;

	printf("== dumping %s, length=%d\n", title, len);

	while (len-- > 0) {
		printf("%.2x ", *buf++);
		if ((++n % 16) == 0)
			putchar('\n');
	}

	if (n % 16)
		putchar('\n');
}

static void print_ip_header(const struct ip *iphdr)
{
	printf("iphdr.ip_hl=%d\n", iphdr->ip_hl);
	printf("iphdr.ip_v=%d\n", iphdr->ip_v);
	printf("iphdr.ip_len=%d\n", ntohs(iphdr->ip_len));
	printf("iphdr.ip_p=%d\n", iphdr->ip_p);
	printf("iphdr.ip_sum=%#x\n", iphdr->ip_sum);
}

static void print_icmp_request(const void *etherbuf, size_t len)
{
	const struct icmphdr *icmphdr;
	const struct ip *iphdr;	/* no option behind */
	const void *icmpdata;

	dump_packet("ICMP request", etherbuf, len);
	iphdr = etherbuf + ETH_HLEN;
	icmphdr = (const struct icmphdr *)(iphdr + 1);
	icmpdata = icmphdr + 1;
	(void)icmpdata;
	printf("icmp.icmp_type=%d\n", icmphdr->type);
	printf("icmp.icmp_id=%d\n", ntohs(icmphdr->un.echo.id));
	printf("icmp.icmp_seq=%d\n", ntohs(icmphdr->un.echo.sequence));

	print_ip_header(iphdr);
}

static struct ip *check_icmp_request(void *etherbuf, size_t len)
{
	struct ip *iphdr;

	if (verbosity > 1)
		print_icmp_request(etherbuf, len);

	if (len < ETH_HLEN + sizeof(struct ip) + sizeof(struct icmphdr))
		return NULL;

	iphdr = etherbuf + ETH_HLEN;

	if (iphdr->ip_hl != 5 || iphdr->ip_v != 4)
		return NULL;

	if (iphdr->ip_p != IPPROTO_ICMP)
		return NULL;

	return iphdr;
}

static size_t build_icmp_reply(uint8_t *o_frame, uint8_t *i_frame,
			size_t len,
			struct ether_addr *dst_mac,
			struct ether_addr *src_mac)
{
	struct icmphdr *s_icmphdr, *d_icmphdr, icmphdr;
	struct ip *s_iphdr, *d_iphdr, iphdr;
	uint16_t cksum;
	size_t pktlen;
	int datalen;
	void *data;

	s_iphdr = check_icmp_request(i_frame, len);
	if (s_iphdr == NULL)
		return -EPROTO;

	s_icmphdr = (struct icmphdr *)(s_iphdr + 1);
	if (s_icmphdr->type != ICMP_ECHO)
		return -EPROTO;

	data = s_icmphdr + 1;
	datalen = ntohs(s_iphdr->ip_len) - (sizeof(iphdr) + sizeof(icmphdr));

	if (verbosity > 1)
		printf("ip_len=%zd, icmp_len=%zd, ip_len=%d, datalen=%d\n",
			sizeof(iphdr), sizeof(struct icmphdr),
			ntohs(s_iphdr->ip_len), datalen);

	/* MAC */
	memcpy(o_frame, dst_mac, ETH_ALEN);
	memcpy(o_frame + ETH_ALEN, src_mac, ETH_ALEN);
	o_frame[ETH_HLEN - 2] = (ETH_P_IP >> 8) & 0xff;
	o_frame[ETH_HLEN - 1] = ETH_P_IP & 0xff;

	/* IPv4 */
	iphdr.ip_hl = 5;
	iphdr.ip_v = 4;
	iphdr.ip_tos = 0;
	iphdr.ip_len = htons(sizeof(iphdr) + sizeof(icmphdr) + datalen);
	iphdr.ip_id = s_iphdr->ip_id;
	iphdr.ip_off = htons(IP_DF);
	iphdr.ip_ttl = 64;
	iphdr.ip_p = IPPROTO_ICMP;
	memcpy(&iphdr.ip_src, &s_iphdr->ip_dst, sizeof(iphdr.ip_src));
	memcpy(&iphdr.ip_dst, &s_iphdr->ip_src, sizeof(iphdr.ip_dst));
	iphdr.ip_sum = 0;
	iphdr.ip_sum = ip_checksum(&iphdr, sizeof(iphdr));
	d_iphdr = (struct ip *)(o_frame + ETH_HLEN);
	memcpy(d_iphdr, &iphdr, sizeof(iphdr));

	/* ICMP */
	icmphdr.type = ICMP_ECHOREPLY;
	icmphdr.code = 0;
	icmphdr.un.echo.id = s_icmphdr->un.echo.id;
	icmphdr.un.echo.sequence = s_icmphdr->un.echo.sequence;
	icmphdr.checksum = 0;
	d_icmphdr = (struct icmphdr *)(o_frame + ETH_HLEN + sizeof(iphdr));
	memcpy(d_icmphdr, &icmphdr, sizeof(icmphdr));
	memcpy(d_icmphdr + 1, data, datalen);
	cksum = ip_checksum(d_icmphdr, sizeof(icmphdr) + datalen);
	d_icmphdr->checksum = cksum;

	pktlen = ETH_HLEN + sizeof(iphdr) + sizeof(icmphdr) + datalen;

	if (verbosity > 1) {
		dump_packet("ICMP reply", o_frame, pktlen);
		print_ip_header(d_iphdr);
	}

	return pktlen;
}

static void usage(void)
{
	fprintf(stderr, "oob-net-icmp -i <network-interface> [-d][-s]\n");
}

int main(int argc, char *argv[])
{
	uint8_t i_frame[ETHER_MAX_LEN], o_frame[ETHER_MAX_LEN];
	int ret, tfd, s, n = 0, c, ifindex;
	const char *netif = NULL;
	struct oob_msghdr msghdr;
	struct sched_param param;
	struct sockaddr_ll addr;
	struct sockaddr hwaddr;
	struct ifreq ifr;
	struct iovec iov;
	ssize_t count;

	while ((c = getopt(argc, argv, "i:ds")) != EOF) {
		switch (c) {
		case 'i':
			netif = optarg;
			break;
		case 'd':
			verbosity = 2;
			break;
		case 's':
			verbosity = 0;
			break;
		default:
			usage();
			exit(1);
		}
	}

	if (netif == NULL) {
		usage();
		exit(2);
	}

	param.sched_priority = 1;
	ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	if (ret)
		error(1, ret, "pthread_setschedparam()");

	tfd = evl_attach_self("oob-net-icmp:%d", getpid());
	if (tfd < 0)
		error(1, -tfd, "cannot attach to the EVL core");

	/*
	 * Get a raw socket with out-of-band capabilities.
	 */
	s = socket(AF_PACKET, SOCK_RAW | SOCK_OOB, 0);
	if (s < 0)
		error(1, errno, "cannot create raw packet socket");

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, netif, IFNAMSIZ - 1);
	ret = ioctl(s, SIOCGIFINDEX, &ifr);
	if (ret < 0)
		error(1, errno, "ioctl(SIOCGIFINDEX)");

	ifindex = ifr.ifr_ifindex;

	ret = ioctl(s, SIOCGIFHWADDR, &ifr);
	if (ret < 0)
		error(1, errno, "ioctl(SIOCGIFHWADDR)");

	hwaddr = ifr.ifr_hwaddr;

	if (verbosity > 1)
		printf("sending via %s (if%d), mac=%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
			ifr.ifr_name, ifindex,
			(uint8_t)hwaddr.sa_data[0],
			(uint8_t)hwaddr.sa_data[1],
			(uint8_t)hwaddr.sa_data[2],
			(uint8_t)hwaddr.sa_data[3],
			(uint8_t)hwaddr.sa_data[4],
			(uint8_t)hwaddr.sa_data[5]);

	/*
	 * NOTE: Unlike the in-band network stack, EVL accepts binding
	 * a packet socket to a VLAN device. The in-band stack will be
	 * told by EVL to bind its side to the real device instead.
	 */
	memset(&addr, 0, sizeof(addr));
	addr.sll_ifindex = ifindex;
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(ETH_P_ALL);
	ret = bind(s, (struct sockaddr *)&addr, sizeof(addr));
	if (ret)
		error(1, errno, "cannot bind packet socket");
	if (verbosity)
		printf("listening to interface %s\n", netif);

	for (;;) {
		iov.iov_base = i_frame;
		iov.iov_len = sizeof(i_frame);
		msghdr.msg_iov = &iov;
		msghdr.msg_iovlen = 1;
		msghdr.msg_control = NULL;
		msghdr.msg_controllen = 0;
		msghdr.msg_name = &addr;
		msghdr.msg_namelen = sizeof(addr);
		msghdr.msg_flags = 0;
		count = oob_recvmsg(s, &msghdr, NULL, 0);
		if (count < 0)
			error(1, errno, "oob_recvmsg() failed");

		if (verbosity)
			printf("[%d] count=%zd, proto=%#hx, ifindex=%d, type=%u, halen=%u, "
				"mac=%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
				n, count,
				ntohs(addr.sll_protocol),
				addr.sll_ifindex,
				addr.sll_pkttype,
				addr.sll_halen,
				(uint8_t)addr.sll_addr[0],
				(uint8_t)addr.sll_addr[1],
				(uint8_t)addr.sll_addr[2],
				(uint8_t)addr.sll_addr[3],
				(uint8_t)addr.sll_addr[4],
				(uint8_t)addr.sll_addr[5]);

		count = build_icmp_reply(o_frame, i_frame, count,
					(struct ether_addr *)addr.sll_addr,
					(struct ether_addr *)hwaddr.sa_data);
		if (count < 0) {
			printf("  *** not an ICMP request - dropped\n");
			continue;
		}

		iov.iov_base = o_frame;
		iov.iov_len = count;
		msghdr.msg_name = &addr;
		/*
		 * The core returned the index of the real network
		 * interface receiving the ICMP request in
		 * addr.sll_ifindex. We need to switch this value back
		 * to the index of the VLAN device which acts as an
		 * oob data port.
		 */
		addr.sll_ifindex = ifindex;
		msghdr.msg_namelen = sizeof(addr);
		msghdr.msg_flags = 0;
		count = oob_sendmsg(s, &msghdr, NULL, 0);
		if (count < 0)
			error(1, errno, "oob_sendmsg() failed");

		if (verbosity > 1)
			printf("  .. ICMP reply sent: %d\n", (int)count);
		n++;
	}

	return 0;
}
