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
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

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

static const char *stack_to_proto(const char *stack)
{
	if (!strcmp(stack, "ip"))
		return "IPv4";
	if (!strcmp(stack, "xia"))
		return "0xc0de";
	err(1, "Unknown stack `%s'", stack);
}

/* XXX add_rule() calls ebtables(8) to add rules because the kernel's
 * interface to do so is not trivial.
 */
static void add_rule(const char *ebtables, const char *stack,
	const char *if_name)
{
	pid_t pid = fork();
	switch (pid) {
	case 0: {
		/* The const qualifier is not being lost here because
		 * execv() is called afterwards.
		 */
		char *argv[] = {(char *)ebtables, "-A", "OUTPUT", "--proto",
			(char *)stack_to_proto(stack), "--out-if",
			(char *)if_name, "--jump", "DROP", NULL};
		execv(ebtables, argv);
		err(1, "Can't exec `%s'", ebtables);
		break; /* Redundancy, execution never reaches here. */
	}

	case -1:
		err(1, "Can't fork");
		break; /* Redundancy, execution never reaches here. */

	default: {
		/* Parent. */
		int status;
		if (waitpid(pid, &status, 0) < 0)
			err(1, "waitpid() failed");
		if (!WIFEXITED(status))
			errx(1, "ebtables(8) at `%s' has terminated abnormally",
				ebtables);
		if (WIFSIGNALED(status))
			errx(1, "ebtables(8) at `%s' was terminated by signal %i",
			ebtables, WTERMSIG(status));
		assert(!WIFSTOPPED(status));
		if (WEXITSTATUS(status))
			errx(1, "ebtables(8) at `%s' has terminated with status %i",
				ebtables, WEXITSTATUS(status));
		break;
	}

	}
}

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

	int i;
	FILE *f;
	double start, diff;

	/* Read parameters. */
	argp_parse(&argp, argc, argv, 0, NULL, &args);

	/* Install ebtables(8) rules. */
	if (args.add_rules) {
		for (i = 0; i < args.count; i++)
			add_rule(args.ebtables, args.stack, args.ifs[i]);
	}

	/* Create parent paths of @args.file. */
	if (args.parents)
		assert(!close(mkdir_parents(args.file)));

	/* Create sampling file. */
	f = fopen(args.file, "w");
	if (!f)
		err(1, "Can't open file `%s'", args.file);
	/* TODO Add header to file. */

	/* TODO Daemonize. */

	start = now();
	while (1) {
		/* TODO Sample measurements and save to file. */
		printf("TODO Collect data!\n");
		if (fflush(f))
			err(1, "Can't save content of file `%s'", args.file);

		diff = now() - start;
		if (diff < args.sleep)
			nsleep(args.sleep - diff);
		start = now();
	}

	assert(!fclose(f));
	end_args(&args);
	return 0;
}
