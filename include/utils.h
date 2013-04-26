#ifndef _UTILS_H
#define _UTILS_H

#include <stdio.h>
#include <assert.h>
#include <argp.h>

long arg_to_long(const struct argp_state *state, const char *arg);

double now(void);

void nsleep(double seconds);

#define printf_fsh(format...) ({		\
	printf(format);				\
	assert(!fflush(stdout));		\
})

#endif	/* _UTILS_H */
