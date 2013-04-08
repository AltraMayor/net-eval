#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <time.h>
#include <linux/rtnetlink.h>
#include <libmnl/libmnl.h>
#include <arpa/inet.h>

#include <rtnl.h>

static void put_ipv4_rtable_add(void *buf, int seq, in_addr_t dst, int mask,
	int iface, in_addr_t gw, int update)
{
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_NEWROUTE;
	nlh->nlmsg_flags = NLM_F_REQUEST |
		(update ? NLM_F_REPLACE : (NLM_F_CREATE | NLM_F_EXCL));
	nlh->nlmsg_seq = seq;

	rtm = mnl_nlmsg_put_extra_header(nlh, sizeof(struct rtmsg));
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = mask;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_protocol = RTPROT_STATIC;
	rtm->rtm_table = RT_TABLE_MAIN;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_flags = 0;

	mnl_attr_put_u32(nlh, RTA_DST, dst);
	mnl_attr_put_u32(nlh, RTA_OIF, iface);
	mnl_attr_put_u32(nlh, RTA_GATEWAY, gw);
}

/* XXX These constants should come from the kernel once XIA goes mainline. */
/* Autonomous Domain Principal */
#define XIDTYPE_AD (__cpu_to_be32(0x10))
/* Host Principal */
#define XIDTYPE_HID (__cpu_to_be32(0x11))
#define AF_XIA 41
#define XRTABLE_MAIN_INDEX 1

static void put_xip_rtable_add(void *buf, int seq, const union net_addr *from,
	const union net_addr *gateway, int update)
{
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;
	struct xia_xid dst, gw;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= RTM_NEWROUTE;
	nlh->nlmsg_flags = NLM_F_REQUEST |
		(update ? NLM_F_REPLACE : (NLM_F_CREATE | NLM_F_EXCL));
	nlh->nlmsg_seq = seq;

	rtm = mnl_nlmsg_put_extra_header(nlh, sizeof(struct rtmsg));
	rtm->rtm_family = AF_XIA;
	rtm->rtm_dst_len = sizeof(dst);
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_protocol = RTPROT_STATIC;
	rtm->rtm_table = XRTABLE_MAIN_INDEX;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_flags = 0;

	dst.xid_type = XIDTYPE_AD;
	memmove(dst.xid_id, from->id, sizeof(dst.xid_id));
	gw.xid_type = XIDTYPE_HID;
	memmove(gw.xid_id, gateway->id, sizeof(gw.xid_id));

	mnl_attr_put(nlh, RTA_DST, sizeof(dst), &dst);
	mnl_attr_put(nlh, RTA_GATEWAY, sizeof(gw), &gw);

	/*
	printf("%i: %i.%i.%i.%i -> %02x%02x%02x%02x\n", seq,
		from->id[0], from->id[1], from->id[2], from->id[3],
		gateway->id[0], gateway->id[1], gateway->id[2], gateway->id[3]);
	*/
}

static int cb_err(const struct nlmsghdr *nlh, void *data)
{
	struct nlmsgerr *err = (void *)(nlh + 1);
	if (err->error != 0)
		errx(1, "message with seq %u has failed: %s\n",
			nlh->nlmsg_seq, strerror(-err->error));
	return MNL_CB_OK;
}

static mnl_cb_t cb_ctl_array[NLMSG_MIN_TYPE] = {
	[NLMSG_ERROR] = cb_err,
};

/* Receive and digest all the acknowledgments from the kernel that
 * are available now; it does not block.
 */
static void process_acks(struct mnl_socket *nl)
{
	int ret, fd, portid;
	fd_set readfds;
	char rcv_buf[MNL_SOCKET_BUFFER_SIZE];
	struct timeval tv = {
		.tv_sec		= 0,
		.tv_usec	= 0,
	};

	/* Is there anything to process? */
	fd = mnl_socket_get_fd(nl);
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	ret = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (ret < 0)
		err(1, "select() failed");

	portid = mnl_socket_get_portid(nl);
	while (ret) {
		assert(FD_ISSET(fd, &readfds));
		ret = mnl_socket_recvfrom(nl, rcv_buf, sizeof(rcv_buf));
		if (ret < 0)
			err(1, "mnl_socket_recvfrom() failed");

		/* Check that everything went fine. */
		ret = mnl_cb_run2(rcv_buf, ret, 0, portid, NULL, NULL,
			cb_ctl_array, MNL_ARRAY_SIZE(cb_ctl_array));
		if (ret == -1)
			err(1, "mnl_cb_run2() failed");

		/* Is there more to read? */
		ret = select(fd + 1, &readfds, NULL, NULL, &tv);
		if (ret < 0)
			err(1, "select() failed");
	}
}

static void send_batch(struct mnl_socket *nl, struct mnl_nlmsg_batch *b)
{
	ssize_t len = mnl_nlmsg_batch_size(b);
	if (mnl_socket_sendto(nl, mnl_nlmsg_batch_head(b), len) != len)
		err(1, "mnl_socket_sendto() failed");
	process_acks(nl);
}

int flush_rtnl_batch(struct rtnl_batch *b)
{
	/* check if there is any message in the batch not sent yet. */
	if (mnl_nlmsg_batch_is_empty(b->batch))
		return 0;

	send_batch(b->nl, b->batch);

	/* this moves the last message that did not fit into the
	 * batch to the head of it. */
	mnl_nlmsg_batch_reset(b->batch);
	return 1;
}

static void add_ipv4_route_to_batch(struct rtnl_batch *b,
	const struct net_prefix *prefix, const struct port *port, int update)
{
	put_ipv4_rtable_add(mnl_nlmsg_batch_current(b->batch), b->seq++,
		prefix->addr.ip, prefix->mask, port->iface, port->gateway.ip,
		update);

	/* Is there room for more messages in this batch? */
	if (!mnl_nlmsg_batch_next(b->batch))
		flush_rtnl_batch(b);
}

static void add_xip_route_to_batch(struct rtnl_batch *b,
	const struct net_prefix *prefix, const struct port *port, int update)
{
	put_xip_rtable_add(mnl_nlmsg_batch_current(b->batch), b->seq++,
		&prefix->addr, &port->gateway, update);

	/* Is there room for more messages in this batch? */
	if (!mnl_nlmsg_batch_next(b->batch))
		flush_rtnl_batch(b);
}

void init_rtnl_batch(struct rtnl_batch *b, const char *stack)
{
	b->snd_buf = malloc(MNL_SOCKET_BUFFER_SIZE * 2);
	assert(b->snd_buf);

	b->nl = mnl_socket_open(NETLINK_ROUTE);
	if (!b->nl)
		err(1, "mnl_socket_open() failed");
	if (mnl_socket_bind(b->nl, 0, MNL_SOCKET_AUTOPID) < 0)
		err(1, "mnl_socket_bind() failed");

	/* The buffer that we use to batch messages is MNL_SOCKET_BUFFER_SIZE
	 * multiplied by 2 bytes long, but we limit the batch to half of it
	 * since the last message that does not fit the batch goes over the
	 * upper boundary, if you break this rule, expect memory corruptions.
	 */
	b->batch = mnl_nlmsg_batch_start(b->snd_buf, MNL_SOCKET_BUFFER_SIZE);
	if (!b->batch)
		err(1, "mnl_nlmsg_batch_start() failed");

	b->seq = time(NULL);

	if (!strcmp(stack, "ip")) {
		b->add_route = add_ipv4_route_to_batch;
	} else if (!strcmp(stack, "xia")) {
		b->add_route = add_xip_route_to_batch;
	} else {
		errx(1, "Stack `%s' is not supported", stack);
	}
}

void end_rtnl_batch(struct rtnl_batch *b)
{
	process_acks(b->nl); /* Last chance to catch errors. */
	mnl_nlmsg_batch_stop(b->batch);
	assert(!mnl_socket_close(b->nl));
	free(b->snd_buf);
}
