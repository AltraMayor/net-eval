#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <err.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#include <netinet/in.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <net/ethernet.h>	/* ETHER_HDR_LEN */

#include <ebt.h>

static const char *stack_to_proto(const char *stack)
{
	if (!strcmp(stack, "ip"))
		return "IPv4";
	if (!strcmp(stack, "xia"))
		return "0xc0de";
	err(1, "Unknown stack `%s'", stack);
}

/* XXX ebt_add_rule() calls ebtables(8) to add rules because the kernel's
 * interface to do so is not trivial.
 */
void ebt_add_rule(const char *ebtables, const char *stack, const char *if_name)
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

int ebt_socket(void)
{
	return socket(AF_INET, SOCK_RAW, PF_INET);
}

void ebt_close(int sk)
{
	assert(!close(sk));
}

static void init_repl(struct ebt_replace *repl)
{
	const char *table = "filter";

	memset(repl, 0, sizeof(*repl));
	memmove(repl->name, table, strlen(table));
}

static struct ebt_replace *retrieve_repl(int sk)
{
	struct ebt_replace *repl;
	socklen_t optlen;
	size_t size;
	struct ebt_counter *counters;
	char *entries;

	repl = malloc(sizeof(*repl));
	assert(repl);
	init_repl(repl);
	optlen = sizeof(*repl);
	if (getsockopt(sk, IPPROTO_IP, EBT_SO_GET_INFO, repl, &optlen) < 0)
		err(1, "getsockopt(EBT_SO_GET_INFO) failed");

	if (!repl->nentries)
		return repl;

	/* Alloc memory for counters and entries. */
	size = repl->nentries * sizeof(*counters);
	counters = malloc(size);
	assert(counters);
	repl->counters = counters;
	optlen += size;
	size = repl->entries_size;
	entries = malloc(size);
	assert(entries);
	repl->entries = entries;
	optlen += size;

	repl->num_counters = repl->nentries;
	if (getsockopt(sk, IPPROTO_IP, EBT_SO_GET_ENTRIES, repl, &optlen) < 0)
		err(1, "getsockopt(EBT_SO_GET_ENTRIES) failed");
	return repl;
}

static void free_repl(struct ebt_replace *repl)
{
	free(repl->entries);
	free(repl->counters);
	free(repl);
}

typedef void (*scan_fun_t)(struct ebt_replace *repl, struct ebt_entry *e,
	struct ebt_counter *cnt, void *arg);

static int _scan_output(struct ebt_entry *e, struct ebt_replace *repl,
	__be16 ethproto, int *pprint, int *pindex, scan_fun_t fun, void *arg)
{
	if (e->bitmask & EBT_ENTRY_OR_ENTRIES) {
		/* An entry. */
		if (!*pprint)
			return 0;
		if (e->ethproto == ethproto)
			fun(repl, e, &repl->counters[*pindex], arg);
		(*pindex)++;
	} else {
		/* A chain. */
		struct ebt_entries *entries = (struct ebt_entries *)e;
		if (*pprint)
			return 1; /* End. */

		if (!strcmp(entries->name, "OUTPUT")) {
			*pprint = 1;
			*pindex = entries->counter_offset;
		}
	}
	return 0;
}

static void scan_output(struct ebt_replace *repl, __be16 ethproto,
	scan_fun_t fun, void *arg)
{
	int print, index;

	if (!repl->nentries)
		return; /* Nothing to do. */

	print = index = 0;
	EBT_ENTRY_ITERATE(repl->entries, repl->entries_size, _scan_output,
		repl, ethproto, &print, &index, fun, arg);
}

static __be16 stack_to_ethproto(const char *stack)
{
	if (!strcmp(stack, "ip"))
		return htons(0x0800);
	if (!strcmp(stack, "xia"))
		return htons(0xc0de);
	err(1, "Unknown stack `%s'", stack);
}

static void write_header(struct ebt_replace *repl, struct ebt_entry *e,
	struct ebt_counter *cnt, void *arg)
{
	FILE *f = arg;
	fprintf(f, " %s.pcnt %s.bcnt", e->out, e->out);
}

void ebt_add_header_to_file(int sk, const char *stack, FILE *f)
{
	struct ebt_replace *repl = retrieve_repl(sk);
	assert(repl);
	fprintf(f, "time");
	scan_output(repl, stack_to_ethproto(stack), write_header, f);
	fprintf(f, "\n");
	free_repl(repl);
}

static void write_samples(struct ebt_replace *repl, struct ebt_entry *e,
	struct ebt_counter *cnt, void *arg)
{
	FILE *f = arg;
	fprintf(f, " %" PRIu64 " %" PRIu64, cnt->pcnt,
		cnt->pcnt * ETHER_HDR_LEN + cnt->bcnt);
}

void ebt_write_sample_to_file(int sk, const char *stack, FILE *f)
{
	time_t now;
	struct tm tm;
	char buffer[128];
	struct ebt_replace *repl;

	/* Add timestamp. */
	now = time(NULL);
	gmtime_r(&now, &tm);
	strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H-%M-%S", &tm);
	fprintf(f, "%s", buffer);

	repl = retrieve_repl(sk);
	assert(repl);
	scan_output(repl, stack_to_ethproto(stack), write_samples, f);
	fprintf(f, "\n");
	free_repl(repl);
}

static void add_samples(struct ebt_replace *repl, struct ebt_entry *e,
	struct ebt_counter *cnt, void *arg)
{
	struct ebt_counter *acc_cnt = arg;
	acc_cnt->pcnt += cnt->pcnt;
	acc_cnt->bcnt += cnt->bcnt;
}

static void ebt_init_cnt(int sk, const char *stack, struct ebt_counter *cnt)
{
	struct ebt_replace *repl = retrieve_repl(sk);
	assert(repl);
	cnt->pcnt = cnt->bcnt = 0;
	scan_output(repl, stack_to_ethproto(stack), add_samples, cnt);
	cnt->bcnt += cnt->pcnt * ETHER_HDR_LEN;
	free_repl(repl);
}

struct ebt_counter *ebt_create_cnt(int sk, const char *stack)
{
	struct ebt_counter *cnt = malloc(sizeof(*cnt));
	if (!cnt)
		return NULL;
	ebt_init_cnt(sk, stack, cnt);
	return cnt;
}

void ebt_free_cnt(struct ebt_counter *cnt)
{
	free(cnt);
}

void ebt_write_rates_to_file(int sk, const char *stack, FILE *f,
	double delta_t, struct ebt_counter *prv_cnt)
{
	struct ebt_counter new_cnt;
	ebt_init_cnt(sk, stack, &new_cnt);
	fprintf(f, "%.1f pps\t%.1f Bps\n",
		(new_cnt.pcnt - prv_cnt->pcnt) / delta_t,
		(new_cnt.bcnt - prv_cnt->bcnt) / delta_t);
	prv_cnt->pcnt = new_cnt.pcnt;
	prv_cnt->bcnt = new_cnt.bcnt;
}
