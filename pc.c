/* Packet Counter. */

#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <argp.h>

#include <net/if.h>		/* if_nametoindex() */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <utils.h>
#include <ebt.h>

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
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {options, parse_opt, adoc, doc};

static char *skip_slash(char *p)
{
	while (*p == '/')
		p++;
	return p;
}

static int _mkdir_parents(int dd, char *file)
{
	char *p = strchr(file, '/');
	int new_dd;

	if (!p) {
		/* There is no folder to create. */
		return dd;
	}

	*p = '\0';
	new_dd = openat(dd, file, O_DIRECTORY);
	if (new_dd < 0) {
		if (errno == ENOENT) {
			if (mkdirat(dd, file, 0770))
				err(1, "Can't create directory `%s'", file);
			new_dd = openat(dd, file, O_DIRECTORY);
		}
		if (new_dd < 0)
			err(1, "Can't open directory `%s'", file);
	}
	assert(!close(dd));
	return _mkdir_parents(new_dd, skip_slash(p + 1));
}

/* Return the directory descriptor where @file should be created. */
static int mkdir_parents(const char *file)
{
	char *copy_file;
	const char *path;
	int dd;

	copy_file = alloca(strlen(file) + 1);
	assert(copy_file);
	strcpy(copy_file, file);

	path = (copy_file[0] == '/') ? "/" : ".";
	dd = open(path, O_DIRECTORY);
	if (dd < 0)
		err(1, "Can't open directory `%s'", path);
	return _mkdir_parents(dd, skip_slash(copy_file));
}

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

		/* This parameter expects something like
		 * "experiment/stack/column/run".
		 */
		.file		= NULL,

		.count		= 0,
		.entries	= 0,
		.ifs		= NULL,
	};

	int i, sk;
	FILE *f;
	double start;
	struct ebt_counter *cnt;

	/* Read parameters. */
	argp_parse(&argp, argc, argv, 0, NULL, &args);

	/* Install ebtables(8) rules. */
	if (args.add_rules) {
		for (i = 0; i < args.count; i++)
			ebt_add_rule(args.ebtables, args.stack, args.ifs[i]);
	}

	/* Create parent paths of @args.file. */
	if (args.file && args.parents)
		assert(!close(mkdir_parents(args.file)));

	/* Create sampling file. */
	sk = ebt_socket();
	if (sk < 0)
		err(1, "Can't get a socket");
	if (args.file) {
		f = fopen(args.file, "w");
		if (!f)
			err(1, "Can't open file `%s'", args.file);
		ebt_add_header_to_file(sk, args.stack, f);
	} else {
		f = stdout;
	}

	/* Daemonize. */
	if (args.daemon && daemon(1, 1))
		err(1, "Can't daemonize");

	start = now();
	if (args.file) {
		ebt_write_sample_to_file(sk, args.stack, f);
		if (fflush(f))
			err(1, "Can't save content of file `%s'", args.file);
	} else {
		cnt = ebt_create_cnt(sk, args.stack);
		assert(cnt);
	}

	while (1) {
		double diff = now() - start;
		if (diff < args.sleep)
			nsleep(args.sleep - diff);
		else
			warnx("Option --sleep=%i is too little; not enough time to estimate rates. Consider increasing the period",
			args.sleep);

		start = now();
		if (args.file)
			ebt_write_sample_to_file(sk, args.stack, f);
		else
			ebt_write_rates_to_file(sk, args.stack, f, args.sleep,
				cnt);
		if (fflush(f))
			err(1, "Can't save content of file `%s'",
				args.file ? args.file : "STDOUT");
	}

	ebt_free_cnt(cnt);
	if (args.file)
		assert(!fclose(f));
	ebt_close(sk);
	end_args(&args);
	return 0;
}
