/* Router Keeper. */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <argp.h>

#include <net/if.h>		/* if_nametoindex()		*/
#include <arpa/inet.h>		/* inet_pton()			*/

#include <utils.h>
#include <seeds.h>
#include <rdist.h>
#include <strarray.h>
#include <rtnl.h>

/* Argp's global variables. */
const char *argp_program_version = "Router keeper 1.0";

/* Arguments:
 *	Network interface of gateway (e.g. 'eth0')
 *	Gateway address
 */
static char adoc[] = "IFNAME GATEWAY [IFNAME GATEWAY]";

static char doc[] = "RK -- populate and update routing table for "
	"evaluation of fowarding performance of routers";

static struct argp_option options[] = {
	{"prefix",	'p', "FILE",	0, "Name of prefix file"},
	{"stack",	's', "NET",	0,
		"Chose between 'ip' and 'xia' stacks"},
	{"load-update",	'l', 0,		0, "Assume updating instead of "
		"creating while loading routing table"},
	{"upd-rate",	'u', "RATE",	0, "Update rate (entrie per second)"},
	{"run",		'r', "RUN",	0, "Run must be >= 1"},
	{ 0 }
};

struct args {
	const char *prefix_filename;
	const char *stack;
	int load_update;
	int update_rate;	/* updates per seconds */
	int run;

	/* Arguments. */
	int count;
	int state;
	int entries;
	struct port *ports;
};

static void make_space(struct args *args)
{
	size_t bytes;
	if (args->entries > args->count)
		return;
	args->entries = !args->entries ? 1 : 2 * args->entries;
	bytes = args->entries * sizeof(*args->ports);
	args->ports = realloc(args->ports, bytes);
	assert(args->ports);
}

static void end_args(struct args *args)
{
	free(args->ports);

	/* Put it into a consistent state. */
	args->count = 0;
	args->state = 0;
	args->entries = 0;
	args->ports = NULL;
}

/* XXX The reason only HIDs are being accepted here is because there's no
 * XIA library that makes handling DAGs easy yet, and we don't want to
 * duplicate even more code before the library is written.
 * The function below should go away once the library is available.
 */
static void assign_addr_hex(struct argp_state *state, union net_addr *addr,
	const char *hex)
{
	const int hex_size = 2 * XIA_XID_MAX;
	const char *p = hex;
	int i;
	
	if (strnlen(hex, hex_size + 1) != hex_size)
		argp_error(state, "ID `%s' is not %i characters long",
			hex, hex_size);
	for (i = 0; i < XIA_XID_MAX; i++) {
		int h;
		if (!sscanf(p, "%2x", &h))
			argp_error(state,
				"Substring `%c%c' in ID `%s' is not hexadecimal",
				p[0], p[1], hex);
		addr->id[i] = h;
		p += 2;
	}
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct args *args = state->input;

	switch (key) {
	case 'p':
		args->prefix_filename = arg;
		break;

	case 's':
		args->stack = arg;
		if (strcmp(arg, "ip") && strcmp(arg, "xia"))
			argp_error(state,
				"Stack must be either 'ip', or 'xia'");
		break;

	case 'l':
		args->load_update = 1;
		assert(!arg);
		break;

	case 'u':
		args->update_rate = arg_to_long(state, arg);
		if (args->update_rate < 0)
			argp_error(state, "Update rate must be >= 0");
		break;

	case 'r':
		args->run = arg_to_long(state, arg);
		if (args->run < 1)
			argp_error(state, "Run must be >= 1");
		break;

	case ARGP_KEY_INIT:
		break;

	case ARGP_KEY_ARG:
		switch (args->state) {
		case 0: {
			int iface = if_nametoindex(arg);
			if (!iface)
				argp_error(state, "Invalid interface `%s'",
					arg);
			make_space(args);
			args->ports[args->count].index = args->count;
			args->ports[args->count].iface = iface;
			args->state++;
			break;
		}
		case 1:
			if (!strcmp(args->stack, "ip")) {
				memset(&args->ports[args->count].gateway, 0,
					sizeof(args->ports[0].gateway));
				if (!inet_pton(AF_INET, arg,
					&args->ports[args->count].gateway.ip))
					argp_error(state,
						"Invalid IP address `%s'", arg);
			} else if (!strcmp(args->stack, "xia")) {
				assign_addr_hex(state,
					&args->ports[args->count].gateway,
					arg);
			} else {
				argp_error(state,
					"Stack `%s' is not supported",
					args->stack);
			}

			args->count++;
			args->state = 0; /* Go back to initial state. */
			break;
		default:
			assert(0);
			break;
		}
		break;

	case ARGP_KEY_END:
		if (args->state)
			argp_error(state, "For each interface must exist "
				"an gateway");
		if (args->count < 1)
			argp_error(state, "There must be at least one pair of "
				"inteface and gateway");
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {options, parse_opt, adoc, doc};

int main(int argc, char **argv)
{
	struct args args = {
		/* Defaults. */
		.prefix_filename	= "prefix",
		.stack			= "ip",
		.load_update		= 0,
		.update_rate		= 0,
		.run			= 1,

		.count			= 0,
		.state			= 0,
		.entries		= 0,
		.ports			= NULL,
	};

	int nnodes = args.count + 1;	/* Ports + Router (1).	*/
	int node_id = nnodes;		/* It is the router.	*/
	struct seed s1, s2, node_seed;
	int force_addr;
	struct net_prefix *prefixes;
	uint64_t prefixes_count, i;
	struct unif_state port_dist, prefix_dist;
	struct rtnl_batch b;
	double start, checkpoint, diff, count;
	struct port **ports;
	int last, upd_to_sleep;

	/* Read parameters. */
	argp_parse(&argp, argc, argv, 0, NULL, &args);

	/* Load seeds. */
	load_seeds(args.run, nnodes, node_id, &s1, &s2, &node_seed);

	/* Load and shuffle destination addresses. */
	force_addr = !!strcmp(args.stack, "ip"); /* Only IP uses CIDR. */
	prefixes = load_file_as_shuffled_addrs(args.prefix_filename,
		&prefixes_count, s1.seeds, SEED_UINT32_N, force_addr);
	if (!prefixes_count)
		err(1, "Prefix file `%s' is empty", args.prefix_filename);

	/* Initialize port numbers. */
	init_unif(&port_dist, s2.seeds, SEED_UINT32_N);
	assign_port(prefixes, prefixes_count, args.count, &port_dist);

	/* Load destinations into routing table. */
	init_rtnl_batch(&b, args.stack);
	printf_fsh("Loading routing table... ");
	start = now();
	for (i = 0; i < prefixes_count; i++) {
		struct net_prefix *pp = &prefixes[i];
		struct port *pt = &args.ports[pp->port];
		rtnl_add_route_to_batch(&b, pp, pt, args.load_update);
	}
	flush_rtnl_batch(&b);
	diff = now() - start;
	if (diff > 0.0)
		printf("%.1f entry/s ", prefixes_count / diff);
	printf_fsh("DONE\n");

	if (args.update_rate <= 0)
		goto out;

	/* Keep updating routing table. */
	init_unif(&prefix_dist, node_seed.seeds, SEED_UINT32_N);
	ports = malloc(sizeof(*ports) * args.count);
	assert(ports);
	for (i = 0; i < args.count; i++)
		ports[i] = &args.ports[i];
	last = args.count - 1;
	upd_to_sleep = args.update_rate;
	count = 0.0;
	checkpoint = start = now();
	while (1) {
		/* Sample destination. */
		uint64_t prefix_sample = sample_unif_0_n1(&prefix_dist,
			prefixes_count);
		struct net_prefix *pp = &prefixes[prefix_sample];

 		/* Sample new gateway. */
		int port_sample;
		struct port *new_port;
		if (pp->port != last) {
			struct port *temp = ports[pp->port];
			ports[pp->port] = ports[last];
			ports[last] = temp;
		}
		port_sample = sample_unif_0_n1(&port_dist, last);
		new_port = ports[port_sample];
		if (pp->port != last) {
			struct port *temp = ports[pp->port];
			ports[pp->port] = ports[last];
			ports[last] = temp;
		}
		assert(pp->port != new_port->index);
		pp->port = new_port->index;

		/* Update routing table. */
		rtnl_add_route_to_batch(&b, pp, new_port, 1);

		count++;
		upd_to_sleep--;
		if (!upd_to_sleep) {
			double last_now, d;

			flush_rtnl_batch(&b);

			last_now = now();
			diff = last_now - start;
			if (diff >= 10.0) {
				printf_fsh("%.1f entry/s\n", count / diff);
				count = 0.0;
				last_now = start = now();
			}

			/* Avoid updating faster than prescribed rate. */
			d = last_now - checkpoint;
			if (d < 0)
				d = 0.0;
			if (d < 1.0)
				nsleep(1.0 - d);
			upd_to_sleep = args.update_rate;
			checkpoint = now();
		}
	}
	free(ports);
	end_unif(&prefix_dist);

out:
	end_rtnl_batch(&b);
	end_unif(&port_dist);
	free_net_prefix(prefixes);
	end_args(&args);
	return 0;
}
