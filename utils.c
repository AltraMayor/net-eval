#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include <utils.h>

long arg_to_long(const struct argp_state *state, const char *arg)
{
	char *end;
	long l = strtol(arg, &end, 0);
	if (!arg)
		argp_error(state, "An integer must be provided");
	if (!*arg || *end)
		argp_error(state, "'%s' is not an integer", arg);
	return l;
}

double now(void)
{
	struct timespec tp;
	assert(!clock_gettime(CLOCK_MONOTONIC, &tp));
	return tp.tv_sec + tp.tv_nsec / 1.0e9;
}

void nsleep(double seconds)
{
	double integer;
	double frac = modf(seconds, &integer);
	struct timespec req = {
		.tv_sec = integer,
		.tv_nsec = frac * 1e9,
	};
	/*printf("Sleeping %li, %li\n", req.tv_sec, req.tv_nsec);*/
	while (1) {
		int rc = nanosleep(&req, &req);
		if (!rc)
			return;
		assert(rc == -1);
		assert(errno == EINTR);
	}
}
