#ifndef _RTNL_H
#define _RTNL_H

#include <libmnl/libmnl.h>
#include <arpa/inet.h>

struct rtnl_batch {
	struct mnl_socket *nl;
	char *snd_buf;
	struct mnl_nlmsg_batch *batch;
	unsigned int seq;
};

void init_rtnl_batch(struct rtnl_batch *b);
void add_ipv4_route_to_batch(struct rtnl_batch *b,
	in_addr_t dst, int mask, int iface, in_addr_t gw, int update);
/* Return true if there was messages to send. */
int flush_rtnl_batch(struct rtnl_batch *b);
void end_rtnl_batch(struct rtnl_batch *b);

#endif	/* _RTNL_H */
