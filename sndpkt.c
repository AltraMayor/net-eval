/* Send an IPv4 packet via raw socket. */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <err.h>

#include <sys/types.h>		/* socket(), sendto()	*/
#include <sys/socket.h>		/* socket(), sendto()	*/
#include <unistd.h>		/* close()		*/
#include <arpa/inet.h>		/* inet_pton(), htons()	*/
#include <net/if.h>		/* if_nametoindex()	*/
#include <netinet/ip.h>		/* struct iphdr, IP_MAXPACKET (== 65535) */
#include <linux/if_ether.h>	/* ETH_P_IP		*/
#include <netpacket/packet.h>	/* See packet(7)	*/
#include <net/ethernet.h>	/* The L2 protocols	*/

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

/* IMPORTANT: @src_ip must be in big endian! */
static char *make_ipv4_template(int packet_size, uint32_t src_ip,
	uint16_t *psum)
{
	char *packet, *payload;
	struct iphdr *ip;
	int i, payload_len;

	assert(packet_size >= IP4_HDRLEN);
	assert(packet_size <= IP_MAXPACKET);
	packet = malloc(packet_size);
	assert(packet);

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

	/*
	 * Fill payload.
	 */

	payload = packet + IP4_HDRLEN;
	payload_len = packet_size - IP4_HDRLEN;
	assert(!(payload_len & 0x01)); /* @payload_len must be even here. */
	payload_len >>= 1;
	for (i = 1; i <= payload_len; i++) {
		payload[0] = (i & 0xff00) >> 8;
		payload[1] = (i & 0xff);
		payload += 2;
	}

	return packet;
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

static inline void engine_send(struct sndpkt_engine *engine)
{
	ssize_t sent = sendto(engine->sk, engine->pkt_template,
		engine->template_len, 0, (struct sockaddr *)&engine->dev,
		sizeof(engine->dev));
	if (sent != engine->template_len)
		warn("sendto() failed with %li", sent);
}

static void ipv4_send_packet(struct sndpkt_engine *engine, union net_addr *addr)
{
	set_ipv4_template(engine->pkt_template, addr->ip,
		engine->cookie.ip.sum);
	engine_send(engine);
}

void init_sndpkt_engine(struct sndpkt_engine *engine, const char *stack,
	const char *ifname, int packet_len,
	const unsigned char *dst_mac, int mac_len,
	const char *dst_addr_type)
{
	uint32_t src_ip;

	assert(!strcmp(stack, "ip")); /* TODO Add support for xia! */

	engine->sk = socket(AF_PACKET, SOCK_DGRAM, 0);
	if (engine->sk < 0)
		err(EXIT_FAILURE, "socket() failed");

	set_dev(&engine->dev, ifname, ETH_P_IP, dst_mac, mac_len);
	/* Put only @ifname in promiscuous mode. */
	assert(!bind(engine->sk, (const struct sockaddr *)&engine->dev,
		sizeof(engine->dev)));

	assert(!strcmp(dst_addr_type, "ip")); /* TODO Add support for xia! */
	inet_pton(AF_INET, "10.0.0.1", &src_ip);
	/* Kernel will add Ethernet header. */
	packet_len -= ETHER_HDR_LEN;
	engine->pkt_template = make_ipv4_template(packet_len, src_ip,
		&engine->cookie.ip.sum);
	engine->template_len = packet_len;
	
	engine->send_packet = ipv4_send_packet;
}

void end_sndpkt_engine(struct sndpkt_engine *engine)
{
	free(engine->pkt_template);
	assert(!close(engine->sk));
}
