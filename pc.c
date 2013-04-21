/* Packet Counter. */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <argp.h>

#include <net/if.h>		/* if_nametoindex() */
#include <utils.h>

/* Argp's global variables. */
const char *argp_program_version = "Packet counter 1.0";

/* Arguments: network interfaces of containers from host's views
 * (e.g. 'veth.test1').
 */
static char adoc[] = "IFNAME [IFNAME]";

static char doc[] = "PC -- sample number of packets and their total size "
	"in bytes transmitted for a given network stack";

static struct argp_option options[] = {
	{"stack",	's', "NET",		0,
		"Chose between 'ip' and 'xia' stacks"},
	{"add-rules",	'r', 0,			0, "Add ebtables(8) rules"},
	{"ebtables",	'e', "FULL-PATH",	0,
		"Fully qualified path to ebtables(8)"},
	{"sleep",	't', "SECONDS",		0,
		"Sleep time between samplings"},
	{"parents",	'p', 0,			0,
		"Make parent directories as needed"},
	{"daemon",	'd', 0,			0,
		"Daemonize after creating file"},
	{"file",	'f', "FILENAME",	0,
		"Fully qualified name of the file to save samplings"},
	{ 0 }
};

struct args {
	const char *stack;
	int add_rules;
	const char *ebtables;
	int sleep;
	int parents;
	int daemon;
	const char *file;

	/* Arguments. */
	int count;
	int entries;
	const char **ifs;
};

static void make_space(struct args *args)
{
	size_t bytes;
	if (args->entries > args->count)
		return;
	args->entries = !args->entries ? 1 : 2 * args->entries;
	bytes = args->entries * sizeof(*args->ifs);
	args->ifs = realloc(args->ifs, bytes);
	assert(args->ifs);
}

static void end_args(struct args *args)
{
	free(args->ifs);

	/* Put it into a consistent state. */
	args->count = 0;
	args->entries = 0;
	args->ifs = NULL;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct args *args = state->input;

	switch (key) {
	case 's':
		args->stack = arg;
		if (strcmp(arg, "ip") && strcmp(arg, "xia"))
			argp_error(state,
				"Stack must be either 'ip', or 'xia'");
		break;

	case 'r':
		args->add_rules = 1;
		assert(!arg);
		break;

	case 'e':
		args->ebtables = arg;
		break;

	case 't':
		args->sleep = arg_to_long(state, arg);
		if (args->sleep < 1)
			argp_error(state, "Sleep period must be >= 1");
		break;

	case 'p':
		args->parents = 1;
		assert(!arg);
		break;

	case 'd':
		args->daemon = 1;
		assert(!arg);
		break;

	case 'f':
		args->file = arg;
		break;

	case ARGP_KEY_INIT:
		break;

	case ARGP_KEY_ARG:
		if (!if_nametoindex(arg))
			argp_error(state, "Invalid interface `%s'", arg);
		make_space(args);
		args->ifs[args->count++] = arg;
		break;

	case ARGP_KEY_END:
		if (args->add_rules && args->count < 1)
			argp_error(state, "There must be at least one "
				"inteface to add");
		if (!args->file)
			argp_error(state, "Option --file is required");
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
		.stack		= "ip",
		.add_rules	= 0,
		.ebtables	= "/sbin/ebtables",
		.sleep		= 10,
		.parents	= 0,
		.daemon		= 0,
		.file		= NULL,

		.count		= 0,
		.entries	= 0,
		.ifs		= NULL,
	};

	/* Read parameters. */
	argp_parse(&argp, argc, argv, 0, NULL, &args);

	/* TODO Install ebtables rules. */

	/* TODO Create full path for file. */

	/* TODO Create sampling file. */

	/* TODO Daemonize. */

	/* TODO Loop: */
		/* TODO Sample measurements and save to file. */
		/* TODO Sleep. */

	end_args(&args);
	return 0;
}
