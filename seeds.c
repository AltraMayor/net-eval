#include <stdio.h>
#include <assert.h>

#include <seeds.h>

static inline FILE *open_seeds(const char *filename)
{
	return fopen(filename, "r");
}

static inline void close_seeds(FILE *seeds)
{
	assert(!fclose(seeds));
}

#define SEED_LINE_SIZE (2 * 4 + 1)

static inline void skip_seeds(FILE *seeds, long n)
{
	assert(!fseek(seeds, n * SEED_LINE_SIZE, SEEK_CUR));
}


static void read_seed_vec(FILE *seeds, uint32_t *vec, int len)
{
	assert(len >= 0);
	while (len--) {
		assert(fscanf(seeds, "%8x\n", vec) == 1);
		vec++;
	}
}

#define SEEDS_FILENAME	"seeds"

void load_seeds(int run, int nnodes, int node_id,
	struct seed *s1, struct seed *s2, struct seed *node_seed)
{
	FILE *seeds;

	assert(run >= 1);
	assert(nnodes >= 1);
	assert(node_id >= 1);
	assert(node_id <= nnodes);

	seeds = open_seeds(SEEDS_FILENAME);
	if (!seeds)
		err(1, "Can't open file `%s'", SEEDS_FILENAME);

	/* Skip previous runs' seeds. */
	skip_seeds(seeds, (run - 1) * (1 + 1 + nnodes) * SEED_UINT32_N);

	read_seed_vec(seeds, s1->seeds, SEED_UINT32_N);
	read_seed_vec(seeds, s2->seeds, SEED_UINT32_N);

	/* Skip other nodes' seeds. */
	skip_seeds(seeds, (node_id - 1) * SEED_UINT32_N);

	read_seed_vec(seeds, node_seed->seeds, SEED_UINT32_N);

	close_seeds(seeds);
}

void print_seed(const char *name, struct seed *s)
{
	int i;
	printf("Seeds of %s:\n", name);
	for (i = 0; i < SEED_UINT32_N; i++)
		printf("%02i: %08x\n", i + 1, s->seeds[i]);
}
