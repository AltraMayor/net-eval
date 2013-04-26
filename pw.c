/* Packet writer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <argp.h>
#include <math.h>

#include <utils.h>
#include <seeds.h>
#include <rdist.h>
#include <strarray.h>
#include <sndpkt.h>

/* Argp's global variables. */
const char *argp_program_version = "Packet writer 1.0";

static char doc[] = "PW -- packet generator for evaluation of "
	"fowarding performance of routers";

static struct argp_option options[] = {
	{"prefix",	'p', "FILE",	0, "Name of prefix file"},
	{"zipf",	'z', "EXP",	0, "Parameter s of Zipf distribution"},
	{"stack",	's', "NET",	0,
		"Chose between 'ip' and 'xia' stacks"},
	{"ifname",	'i', "IF",	0,
		"Network interface to send packets (e.g. 'eth0')"},
	{"dmac",	'm', "MAC",	0,
		"Ethernet address of router (e.g. '11:22:33:44:55:66')"},
	{"daddr-type",	't', "TYPE",	0,
		"Type of the destination address template {'ip', 'fb0', "
		"'fb1', 'fb2', 'fb3', 'via'}"},
	{"pkt-len",	'l', "LEN",	0, "Packet lenght in bytes"},
	{"nnodes",	'n', "COUNT",	0,
		"Number of nodes (= number of ports + 1)"},
	{"node-id",	'd', "ID",	0,
		"ID of this packet writer [1..(N-1)]"},
	{"run",		'r', "RUN",	0, "Run must be >= 1"},
	{"interactive",	'v', NULL,	0,
		"Allow one to interactively control the number of packets sent"
		},
	{ 0 }
};

struct args {
	const char *prefix_filename;
	double s;
	const char *stack;
	const char *ifname;
	unsigned char dst_mac[32];
	int dst_mac_len;
	const char *dst_addr_type;
	int packet_len;
	int nnodes;
	int node_id;
	int run;
	int interactive;
};

/* XXX Copied from xiaconf/xip/utils.c. This function should go to
 * an XIA library.
 */
static inline int hexd_to_val(int ch)
{
	if ('0' <= ch && ch <= '9')
		return ch - '0' + 0;
	if ('A' <= ch && ch <= 'F')
		return ch - 'A' + 10;
	if ('a' <= ch && ch <= 'f')
		return ch - 'a' + 10;
	return -1;
}

/* XXX Copied from xiaconf/xip/utils.c. This function should go to
 * an XIA library.
 */
static int lladdr_pton(const char *str, unsigned char *lladdr, int alen)
{
	const char *p = str;	/* String cursor.			*/
	int v;		/* Temporary value.				*/
	int octet;	/* The octet being unconvered.			*/
	int digit = 0;	/* State of the machine.			*/
	int count = 0;	/* Number of octets pushed into @lladdr.	*/

	while (*p && alen > 0) {
		switch (digit) {
		case 0:
			/* We expect a hexdigit. */
			v = hexd_to_val(*p);
			if (v < 0)
				return -1;
			octet = v;
			digit++;
			break;
		case 1:
			/* We expect a hexdigit or ':'. */
			if (*p == ':') {
				*(lladdr++) = octet;
				alen--;
				count++;
			} else {
				v = hexd_to_val(*p);
				if (v < 0)
					return -1;
				octet = (octet << 4) + v;
			}
			digit++;
			break;
		case 2:
			/* We expect a ':'. */
			if (*p == ':') {
				*(lladdr++) = octet;
				alen--;
				count++;
			} else
				return -1;
			digit = 0;
			break;
		default:
			return -1;
		}
		p++;
	}

	/* The tests read as follows:
	 *	1. String isn't fully parsed.
	 *	2. There's a more octet to add, but @lladdr is full.
	 *	3. No octet was found.
	 */
	if (*p || (digit && alen <= 0) || (!digit && !count))
		return -1;

	if (digit) {
		assert(alen > 0);
		*lladdr = octet;
		count++;
	}

	return count;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct args *args = state->input;

	switch (key) {
	case 'p':
		args->prefix_filename = arg;
		break;

	case 'z': {
		char *end;
		args->s = strtod(arg, &end);
		if (!*arg || *end)
			argp_error(state, "'%s' is not a float", arg);
		if (args->s < 0 || args->s == NAN || args->s == INFINITY)
			argp_error(state,"Zipf must be >= 0");
		break;
	}

	case 's':
		args->stack = arg;
		if (strcmp(arg, "ip") && strcmp(arg, "xia"))
			argp_error(state,
				"Stack must be either 'ip', or 'xia'");
		break;

	case 'i':
		args->ifname = arg;
		break;

	case 'm':
		args->dst_mac_len = lladdr_pton(arg, args->dst_mac,
			sizeof(args->dst_mac));
		if (args->dst_mac_len < 1)
			argp_error(state,
				"'%s' is not a valid Ethernet address", arg);
		break;

	case 't':
		args->dst_addr_type = arg;
		if (strcmp(arg, "ip") && strcmp(arg, "fb0") &&
			strcmp(arg, "fb1") && strcmp(arg, "fb2") &&
			strcmp(arg, "fb3") && strcmp(arg, "via"))
			argp_error(state, "'%s' is not a valid type", arg);
		break;

	case 'l':
		args->packet_len = arg_to_long(state, arg);
		if (args->packet_len < 1)
			argp_error(state, "Packet lenght must be >= 1");
		break;

	case 'n':
		args->nnodes = arg_to_long(state, arg);
		if (args->nnodes < 2)
			argp_error(state, "Number of nodes must be >= 2");
		break;

	case 'd':
		args->node_id = arg_to_long(state, arg);
		if (args->node_id < 1)
			argp_error(state, "Node ID must be >= 1");
		break;

	case 'r':
		args->run = arg_to_long(state, arg);
		if (args->run < 1)
			argp_error(state,"Run must be >= 1");
		break;

	case 'v':
		args->interactive = 1;
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {options, parse_opt, NULL, doc};

static void drop_line(void)
{
	int ch;
	do {
		ch = getchar();
		if (ch == EOF)
			exit(0);
	} while (ch != '\n');
}

static int ask_count(void)
{
	int read, n;
	do {
		char tail;
		printf(">");
		read = scanf("%i%c", &n, &tail);
		if (!read || tail != '\n') {
			drop_line(); /* Drop uninterpreted chars. */
			printf("Please enter the number of packets to send\n");
			read = 0;
		} else if (n <= 0) {
			printf("Please enter only positive numbers\n");
			read = 0;
		}
	} while (!read);
	return n;
}

int main(int argc, char **argv)
{
	struct args args = {
		/* Defaults. */
		.prefix_filename	= "prefix",
		.s			= 1.0,
		.stack			= "ip",
		.ifname			= "eth0",
		.dst_mac		= {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		.dst_mac_len		= 6,
		.dst_addr_type		= "ip",
		.packet_len		= 64,
		.nnodes			= 3,
		.node_id		= 1,
		.run			= 1,
		.interactive		= 0,
	};

	struct seed s1, s2, node_seed;
	struct net_prefix *prefixes;
	uint64_t prefixes_count;
	struct zipf_cache zcache;
	struct sndpkt_engine engine;
	double start, diff, count, to_send;
	long index;

	/* Read parameters. */
	argp_parse(&argp, argc, argv, 0, NULL, &args);

	/* Load seeds. */
	load_seeds(args.run, args.nnodes, args.node_id, &s1, &s2, &node_seed);
	/*
	print_seed("s1", &s1);
	print_seed("s2", &s2);
	print_seed("node_seed", &node_seed);
	*/

	/* PW does not use seed @s2. */

	/* Load and shuffle destination addresses. */
	prefixes = load_file_as_shuffled_addrs(args.prefix_filename,
		&prefixes_count, s1.seeds, SEED_UINT32_N, 1);
	if (!prefixes_count)
		err(1, "Prefix file `%s' is empty", args.prefix_filename);

	/* Cache Zipf sampling. */
	printf_fsh("Initializing Zipf cache... ");
	init_zipf_cache(&zcache, prefixes_count * 30, args.s, prefixes_count,
		node_seed.seeds, SEED_UINT32_N);
	printf_fsh("DONE\n");
	/*
	print_zipf_cache(&zcache);
	*/

	/* Sample destinations and send packets out. */
	init_sndpkt_engine(&engine, args.stack, args.ifname, args.packet_len,
		args.dst_mac, args.dst_mac_len, args.dst_addr_type);
	index = sample_zipf_cache(&zcache);
	count = 0.0;
	to_send = args.interactive ? ask_count() : 0.0;
	start = now();
	while (1) {
		if (!sndpkt_send(&engine, &prefixes[index - 1].addr))
			continue; /* No packet sent. */
		index = sample_zipf_cache(&zcache);
		count++;

		if (!args.interactive) {
			diff = now() - start;
			if (diff >= 10.0) {
				printf_fsh("%.1f pps\n", count / diff);
				count = 0.0;
				start = now();
			}
		} else {
			printf("Packet %.0f sent\n", count);
			if (count >= to_send)
				to_send = count + ask_count();
		}
	}

	end_sndpkt_engine(&engine);
	end_zipf_cache(&zcache);
	free_net_prefix(prefixes);
	return 0;
}
