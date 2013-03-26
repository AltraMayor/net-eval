#ifndef _RTNL_H
#define _RTNL_H

#include <arpa/inet.h>

int rtnl_ipv4_rtable_add(struct mnl_socket *nl, int iface, in_addr_t dst,
	int mask, in_addr_t gw, int update);

#endif	/* _RTNL_H */
