/* Send an IPv4 packet via raw socket. */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#include <sys/types.h>		/* socket(), sendto()	*/
#include <sys/socket.h>		/* socket(), sendto()	*/
#include <unistd.h>		/* close()		*/
#include <arpa/inet.h>		/* inet_pton(), htons()	*/
#include <net/if.h>		/* if_nametoindex()	*/
#include <netinet/ip.h>		/* struct iphdr, IP_MAXPACKET (== 65535) */
#include <linux/if_ether.h>	/* ETH_P_IP		*/
#include <netpacket/packet.h>	/* See packet(7)	*/
#include <net/ethernet.h>	/* The L2 protocols	*/

#include <net/xia.h>
#include <net/xia_route.h>

#include <sndpkt.h>

#define IP4_HDRLEN		(sizeof(struct iphdr))

/* Sum 16-bit words beginning at location @addr for @len bytes.
 * IMPORTANT: @len must be even.
 */
static uint16_t sum16(void *addr, int len, uint16_t start)
{
	uint32_t sum = start;
	uint16_t *p = addr;

	assert(!(len & 0x01)); /* @len must be even here. */
	len >>= 1;

	while (len--)
		sum += *(p++);
	while (sum >= 0x10000)
		sum = (sum >> 16) + (sum & 0xffff);

	return sum;
}

static void fill_payload(char *payload, int payload_len)
{
	int i;

	assert(!(payload_len & 0x01)); /* @payload_len must be even here. */
	payload_len >>= 1;
	for (i = 1; i <= payload_len; i++) {
		payload[0] = (i & 0xff00) >> 8;
		payload[1] = (i & 0xff);
		payload += 2;
	}
}

/* IMPORTANT: @src_ip must be in big endian! */
static void make_ipv4_template(char *packet, int packet_size, uint32_t src_ip,
	uint16_t *psum)
{
	struct iphdr *ip;

	assert(packet_size >= IP4_HDRLEN);
	assert(packet_size <= IP_MAXPACKET);

	/*
	 *	Fill IPv4 header.
	 */

	ip = (struct iphdr *)packet;
	/* Internet Protocol Version. */
	ip->version = 4;
	/* Header length in 32-bit words. */
	ip->ihl = IP4_HDRLEN / 4;
	/* Type of service. */
	ip->tos = 0;
	/* Total length of datagram. */
	ip->tot_len = htons(packet_size);
	/* ID sequence number; not used for single datagram. */
	ip->id = htons(0);
	/* Flags, and Fragmentation offset (3, 13 bits);
	 * Set flag Don't Fragment (DF), offset not used for single datagram.
	 */
	ip->frag_off = htons(0x4000);
	/* Time To Live; default to maximum value. */
	ip->ttl = 255;
	/* Transport layer protocol.
	 * According to the reference below, the number used here is intended
	 * for experimentation and testing.
	 * http://www.iana.org/assignments/protocol-numbers/protocol-numbers.xml
	 */
	ip->protocol = 253;
	/* Source address. */
	ip->saddr = src_ip;
	/* Destination address. */
	ip->daddr = htonl(0);
	/* Header checksum. Set to 0 when calculating checksum. */
	ip->check = 0;
	*psum = sum16(ip, IP4_HDRLEN, 0);

	fill_payload(packet + IP4_HDRLEN, packet_size - IP4_HDRLEN);
}

/* IMPORTANT: @dst_ip must be in big endian! */
static void set_ipv4_template(char *template, uint32_t dst_ip, uint16_t sum)
{
	struct iphdr *ip = (struct iphdr *)template;
	ip->daddr = dst_ip;

	/* Update checksum.
	 * For details see RFC1071, Computing the Internet Checksum:
	 * https://tools.ietf.org/rfc/rfc1071.txt
	 */
	ip->check = ~ sum16(&dst_ip, sizeof(dst_ip), sum);
}

/* XXX This constant should come from the kernel once XIA goes mainline. */
/* Autonomous Domain Principal */
#define XIDTYPE_AD (__cpu_to_be32(0x10))

static int fill_dst_dag(struct xiphdr *xip, int packet_size)
{
	static const struct xia_row unknown_ad[] = {
		{.s_xid = {.xid_type = XIDTYPE_AD,
			.xid_id = {0,  1,  2,  3,  4,  5, 6, 7, 8, 9,
				  10, 11, 12, 13, 14, 15, 0, 0, 0, 1}},
			.s_edge.i = XIA_EMPTY_EDGES},
		{.s_xid = {.xid_type = XIDTYPE_AD,
			.xid_id = {0,  1,  2,  3,  4,  5, 6, 7, 8, 9,
				  10, 11, 12, 13, 14, 15, 0, 0, 0, 2}},
			.s_edge.i = XIA_EMPTY_EDGES},
		{.s_xid = {.xid_type = XIDTYPE_AD,
			.xid_id = {0,  1,  2,  3,  4,  5, 6, 7, 8, 9,
				  10, 11, 12, 13, 14, 15, 0, 0, 0, 3}},
			.s_edge.i = XIA_EMPTY_EDGES},
		{.s_xid = {.xid_type = XIDTYPE_AD,
			.xid_id = {0,  1,  2,  3,  4,  5, 6, 7, 8, 9,
				  10, 11, 12, 13, 14, 15, 0, 0, 0, 4}},
			.s_edge.i = XIA_EMPTY_EDGES},
	};
	int hdr_len = xip_hdr_len(xip);

	if (packet_size < hdr_len)
		errx(1, "Packet size must be larger or equal to %i",
			hdr_len + ETHER_HDR_LEN);
	memmove(xip->dst_addr, unknown_ad,
		xip->num_dst * sizeof(struct xia_row));
	return hdr_len;
}

static void make_xia_template(char *packet, int packet_size,
	const char *dst_addr_type, int *poffset)
{
	struct xiphdr *xip;
	int hdr_len;

	assert(packet_size >= MIN_XIP_HEADER);
	assert(packet_size <= MAX_XIP_HEADER + XIP_MAXPLEN);

	/*
	 *	Fill XIP header.
	 */

	xip = (struct xiphdr *)packet;
	xip->version = 1;
	xip->next_hdr = 0;
	xip->hop_limit = 255;
	xip->num_src = 0;
	xip->last_node = XIA_ENTRY_NODE_INDEX;

	/* Insert destination. */
	if (!strcmp(dst_addr_type, "fb0")) {
		xip->num_dst = 1;
		hdr_len = fill_dst_dag(xip, packet_size);
		xip->dst_addr[0].s_edge.a[0] = 0;
	} else if (!strcmp(dst_addr_type, "fb1")) {
		xip->num_dst = 2;
		hdr_len = fill_dst_dag(xip, packet_size);
		xip->dst_addr[1].s_edge.a[0] = 0;
		xip->dst_addr[1].s_edge.a[1] = 1;
	} else if (!strcmp(dst_addr_type, "fb2")) {
		xip->num_dst = 3;
		hdr_len = fill_dst_dag(xip, packet_size);
		xip->dst_addr[2].s_edge.a[0] = 0;
		xip->dst_addr[2].s_edge.a[1] = 1;
		xip->dst_addr[2].s_edge.a[2] = 2;
	} else if (!strcmp(dst_addr_type, "fb3")) {
		xip->num_dst = 4;
		hdr_len = fill_dst_dag(xip, packet_size);
		xip->dst_addr[3].s_edge.a[0] = 0;
		xip->dst_addr[3].s_edge.a[1] = 1;
		xip->dst_addr[3].s_edge.a[2] = 2;
		xip->dst_addr[3].s_edge.a[3] = 3;
	} else if (!strcmp(dst_addr_type, "via")) {
		xip->num_dst = 2;
		hdr_len = fill_dst_dag(xip, packet_size);
		xip->dst_addr[0].s_edge.a[0] = 1;
		xip->dst_addr[1].s_edge.a[0] = 0;
	} else {
		errx(1, "Destination type `%s' is not valid", dst_addr_type);
	}

	*poffset = xip_hdr_size(xip->num_dst - 1, 0) + sizeof(xid_type_t);
	xip->payload_len = htons(packet_size - hdr_len);
	fill_payload(packet + hdr_len, xip->payload_len);
}

static inline void set_xia_template(char *template, int offset,
	union net_addr *addr)
{
	memmove(template + offset, addr->id, sizeof(addr->id));
}

static void set_dev(struct sockaddr_ll *dev, const char *ifname, int proto,
	const unsigned char *dst_mac, int len)
{
	dev->sll_family = AF_PACKET;
	dev->sll_protocol = htons(proto);

	/* Resolve interface index. */
	dev->sll_ifindex = if_nametoindex(ifname);
	if (!dev->sll_ifindex)
		err(EXIT_FAILURE, "if_nametoindex(\"%s\") failed", ifname);

	/* These types only make sense for receiving. See packet(7). */
	dev->sll_hatype = 0;
	dev->sll_pkttype = 0;

	assert(len <= sizeof(dev->sll_addr));
	dev->sll_halen = len;
	memmove(dev->sll_addr, dst_mac, len);
}

static inline int engine_send(struct sndpkt_engine *engine)
{
	ssize_t sent = sendto(engine->sk, engine->pkt_template,
		engine->template_len, MSG_DONTWAIT,
		(struct sockaddr *)&engine->dev, sizeof(engine->dev));
	if (sent == engine->template_len)
		return 1;

	assert(sent == -1);
	switch (errno) {
	case ENOBUFS:
	case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
		break;

	default:
		warn("sendto() failed with %li", sent);
		break;
	}
	return 0;
}

static int ipv4_send_packet(struct sndpkt_engine *engine, union net_addr *addr)
{
	set_ipv4_template(engine->pkt_template, addr->ip,
		engine->cookie.ip.sum);
	return engine_send(engine);
}

static int xia_send_packet(struct sndpkt_engine *engine, union net_addr *addr)
{
	set_xia_template(engine->pkt_template, engine->cookie.xia.offset, addr);
	return engine_send(engine);
}

/* XXX Once XIA has gone mainline, this define should come from the kernel. */
#define ETH_P_XIP	0xC0DE

void init_sndpkt_engine(struct sndpkt_engine *engine, const char *stack,
	const char *ifname, int packet_len,
	const unsigned char *dst_mac, int mac_len,
	const char *dst_addr_type)
{
	engine->sk = socket(AF_PACKET, SOCK_DGRAM, 0);
	if (engine->sk < 0)
		err(EXIT_FAILURE, "socket() failed");

	packet_len -= ETHER_HDR_LEN; /* Kernel will add Ethernet header. */
	engine->template_len = packet_len;
	engine->pkt_template = malloc(packet_len);
	assert(engine->pkt_template);

	if (!strcmp(stack, "ip")) {
		uint32_t src_ip;

		assert(!strcmp(dst_addr_type, "ip"));
		inet_pton(AF_INET, "10.0.0.1", &src_ip);

		set_dev(&engine->dev, ifname, ETH_P_IP, dst_mac, mac_len);
		make_ipv4_template(engine->pkt_template, packet_len,
			src_ip, &engine->cookie.ip.sum);
		engine->send_packet = ipv4_send_packet;
	} else if (!strcmp(stack, "xia")) {
		set_dev(&engine->dev, ifname, ETH_P_XIP, dst_mac, mac_len);
		make_xia_template(engine->pkt_template, packet_len,
			dst_addr_type, &engine->cookie.xia.offset);
		engine->send_packet = xia_send_packet;
	} else {
		errx(1, "Stack `%s' is not valid", stack);
	}

	/* Put only @ifname in promiscuous mode. */
	assert(!bind(engine->sk, (const struct sockaddr *)&engine->dev,
		sizeof(engine->dev)));
}

void end_sndpkt_engine(struct sndpkt_engine *engine)
{
	free(engine->pkt_template);
	assert(!close(engine->sk));
}
