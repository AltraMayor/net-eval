/* Packet writer. */

#include <stdio.h>
#include <err.h>

#include <seeds.h>
#include <rdist.h>
#include <strarray.h>

static void shuffle_array(char **array, uint64_t size, struct seed *s)
{
	struct unif_state shuffle_dist;

	if (size <= 1)
		return;

	init_unif(&shuffle_dist, s->seeds, SEED_UINT32_N);
	do {
		uint64_t b = sample_unif_0_n1(&shuffle_dist, size);
		if (b) {
			/* b is not index 0, so swap array[0] and array[b]. */
			char *tmp = array[0];
			array[0] = array[b];
			array[b] = tmp;
		}
		array++;
		size--;
	} while (size > 1);
	end_unif(&shuffle_dist);
}

int main(void)
{
	const char *prefix_filename = "prefix-2013-03-17-14-00";
	double s = 1.0;

	int run = 1;
	int nnodes = 3;
	int node_id = 2;
	
	struct seed s1, s2, node_seed;
	uint64_t prefix_array_size;
	char **prefix_array;
	struct zipf_cache zcache;

	/* TODO Read parameters:
	 *	Prefix file name
	 *	The s parameter of Zipf distribution
	 *	Stack ("ip" or "xia")
	 *	Network interface (e.g. "eth0")
	 *	Ethernet address of router (e.g. "11:22:33:44:55:66")
	 *	Destination address template ("ip", "fb0", ..., "fb3", "via")
	 *	Packet size
	 *	Number of nodes (Packet writers plus router)
	 *	ID of this packet writer (1..(N-1))
	 *	Run (1..)
	 */

	/* Load seeds. */
	load_seeds(run, nnodes, node_id, &s1, &s2, &node_seed);
	/*
	print_seed("s1", &s1);
	print_seed("s2", &s2);
	print_seed("node_seed", &node_seed);
	*/

	/* Load and shuffle prefix destinations. */
	prefix_array = load_file_as_array(prefix_filename, &prefix_array_size);
	if (!prefix_array_size)
		err(1, "Prefix file `%s' is empty", prefix_filename);
	shuffle_array(prefix_array, prefix_array_size, &s1);
	/*
	print_array(prefix_array, prefix_array_size);
	*/

	/* TODO Convert destinations to a binary vector. */
	free_array(prefix_array);

	/* Cache Zipf sampling. */
	printf("Initializing Zipf cache... ");
	fflush(stdout);
	init_zipf_cache(&zcache, prefix_array_size * 30, s, prefix_array_size,
		node_seed.seeds, SEED_UINT32_N);
	printf("DONE\n");
	/*
	print_zipf_cache(&zcache);
	*/

	/* TODO Packet template. */
	/* TODO Loop: */
		/* TODO Sample destination and update packet template. */
		/* TODO Send packet out. */

	end_zipf_cache(&zcache);
	return 0;
}
