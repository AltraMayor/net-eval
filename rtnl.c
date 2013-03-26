#include <err.h>
#include <time.h>
#include <libmnl/libmnl.h>
#include <linux/rtnetlink.h>

#include <rtnl.h>

static int nl_request_wait_reply(struct mnl_socket *nl, struct nlmsghdr *nlh)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	int bytes;

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0)
		err(1, "mnl_socket_sendto() failed");
	bytes = mnl_socket_recvfrom(nl, buf, sizeof(buf));
	if (bytes < 0)
		err(1, "mnl_socket_recvfrom() failed");

	return mnl_cb_run(buf, bytes, nlh->nlmsg_seq,
		mnl_socket_get_portid(nl), NULL, NULL);
}

int rtnl_ipv4_rtable_add(struct mnl_socket *nl, int iface, in_addr_t dst,
	int mask, in_addr_t gw, int update)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_NEWROUTE;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK |
		(update ? NLM_F_REPLACE : NLM_F_EXCL);
	nlh->nlmsg_seq = time(NULL);	/* XXX Get a better sequence. */

	rtm = mnl_nlmsg_put_extra_header(nlh, sizeof(struct rtmsg));
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = mask;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_protocol = RTPROT_STATIC;
	rtm->rtm_table = RT_TABLE_MAIN;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_scope = gw ? RT_SCOPE_UNIVERSE : RT_SCOPE_LINK;
	rtm->rtm_flags = 0;

	mnl_attr_put_u32(nlh, RTA_DST, dst);
	mnl_attr_put_u32(nlh, RTA_OIF, iface);
	if (gw)
		mnl_attr_put_u32(nlh, RTA_GATEWAY, gw);

	return nl_request_wait_reply(nl, nlh);
}
