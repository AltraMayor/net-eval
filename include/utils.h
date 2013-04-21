#ifndef _UTILS_H
#define _UTILS_H

#include <argp.h>

long arg_to_long(const struct argp_state *state, const char *arg);

double now(void);

void nsleep(double seconds);

#endif	/* _UTILS_H */
