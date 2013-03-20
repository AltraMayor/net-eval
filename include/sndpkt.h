#ifndef _SNDPKT_H
#define _SNDPKT_H

#include <stdint.h>
#include <strarray.h>		/* union net_addr	*/
#include <netpacket/packet.h>	/* struct sockaddr_ll	*/

union sndpkt_cookie {
	struct {
		uint16_t sum;
	} ip;
	struct {
		int offset;
	} xia;
};

struct sndpkt_engine {
	int sk; /* Socket. */
	struct sockaddr_ll dev;
	char *pkt_template;
	int template_len;
	union sndpkt_cookie cookie;
	int (*send_packet)(struct sndpkt_engine *engine, union net_addr *addr);
};

void init_sndpkt_engine(struct sndpkt_engine *engine, const char *stack,
	const char *ifname, int packet_len,
	const unsigned char *dst_mac, int mac_len,
	const char *dst_addr_type);

/* IMPORTANT: This function does NOT support multiple threads!
 * RETURN 1 if packet send, 0 otherwise.
 * A likely reason for that is `No buffer space available'.
 */
static inline int sndpkt_send(struct sndpkt_engine *engine,
	union net_addr *addr)
{
	return engine->send_packet(engine, addr);
}

void end_sndpkt_engine(struct sndpkt_engine *engine);

#endif	/* _SNDPKT_H */
