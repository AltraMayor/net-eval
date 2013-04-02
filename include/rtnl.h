#ifndef _RTNL_H
#define _RTNL_H

#include <strarray.h>

struct port {
	int index;	/* XXX Is this field really necessary?	*/
	int iface;	/* XXX Only IP uses this field!		*/
	union net_addr gateway;
};

struct rtnl_batch;

typedef void (*add_route_to_batch_t)(struct rtnl_batch *b,
	const struct net_prefix *prefix, const struct port *port, int update);

struct rtnl_batch {
	struct mnl_socket *nl;
	char *snd_buf;
	struct mnl_nlmsg_batch *batch;
	unsigned int seq;
	add_route_to_batch_t add_route;
};

void init_rtnl_batch(struct rtnl_batch *b, const char *stack);
/* Return true if there was messages to send. */
int flush_rtnl_batch(struct rtnl_batch *b);
void end_rtnl_batch(struct rtnl_batch *b);

static inline void rtnl_add_route_to_batch(struct rtnl_batch *b,
	const struct net_prefix *prefix, const struct port *port, int update)
{
	b->add_route(b, prefix, port, update);
}

#endif	/* _RTNL_H */
