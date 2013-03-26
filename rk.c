/* Router Keeper. */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <argp.h>

#include <net/if.h>		/* if_nametoindex()		*/
#include <arpa/inet.h>		/* inet_pton()			*/

#include <utils.h>

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
	{"upd-rate",	'u', "RATE",	0, "Update rate (entrie per second)"},
	{"run",		'r', "RUN",	0, "Run must be >= 1"},
	{ 0 }
};

struct port {
	int iface;
	union net_addr gateway;
};

struct args {
	const char *prefix_filename;
	const char *stack;
	int update_rate;	/* updates per seconds */
	int run;

	/* Arguments. */
	int count;
	int state;
	int entries;
	struct port *ports;
};

static void make_space(struct port **ports, int *pentries, int count)
{
	size_t bytes;
	if (*pentries > count)
		return;
	*pentries = (!*pentries) ? 1 : 2 * (*pentries);
	bytes = (*pentries) * sizeof(**ports);
	*ports = realloc(*ports, bytes);
	assert(*ports);
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

static error_t parse_opt (int key, char *arg, struct argp_state *state)
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

	case 'u':
		args->update_rate = arg_to_long(state, arg);
		if (args->update_rate < 0)
			argp_error(state,"Update rate must be >= 0");
		break;

	case 'r':
		args->run = arg_to_long(state, arg);
		if (args->run < 1)
			argp_error(state,"Run must be >= 1");
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
			make_space(&args->ports, &args->entries, args->count);
			args->ports[args->count].iface = iface;
			args->state++;
			break;
		}
		case 1:
			/* TODO Add support for XIA. */
			assert(!strcmp(args->stack, "ip"));
			memset(&args->ports[args->count].gateway, 0,
				sizeof(args->ports[0].gateway));
			if (!inet_pton(AF_INET, arg,
				&args->ports[args->count].gateway.ip))
				argp_error(state, "Invalid IP address `%s'",
					arg);
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
		.update_rate		= 0,
		.run			= 1,

		.count			= 0,
		.state			= 0,
		.entries		= 0,
		.ports			= NULL,
	};

	int nnodes = args.count + 1;	/* Ports + Router (1).	*/
	int node_id = nnodes;		/* It is the router.	*/

	/* Read parameters. */
	argp_parse(&argp, argc, argv, 0, NULL, &args);

	/* TODO Load seeds. */

	/* TODO Load and shuffle destinations. */

	/* TODO Convert destinations to a binary vector. */

	/* TODO Load destinations into routing table. */
	/* TODO Use batch updates! */

	/* TODO Loop: */
		/* TODO Sample destination and new gateway. */
		/* TODO Update routing table. */

	end_args(&args);
	return 0;
}
