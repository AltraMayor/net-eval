#ifndef _SEEDS_H
#define _SEEDS_H

#include <stdint.h>

#define SEED_UINT32_N	10

struct seed {
	uint32_t seeds[SEED_UINT32_N];
};

/* Load seeds.
 *
 *	@run starts at 1.
 *
 *	@node_id must be in [1..nnodes].
 *
 *	Seeds @s1 and @s2 change per @run.
 *
 *	Seed @node_seed changes per (@run, @node_id).
 */
void load_seeds(int run, int nnodes, int node_id,
	struct seed *s1, struct seed *s2, struct seed *node_seed);

/* Print seeds; it's useful for debuging. */
void print_seed(const char *name, struct seed *s);

#endif	/* _SEEDS_H */
