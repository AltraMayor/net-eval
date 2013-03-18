/* Packet writer. */

#include <stdio.h>

#include <seeds.h>
#include <rdist.h>

int main(void)
{
	int run = 1;
	int nnodes = 3;
	int node_id = 2;
	
	struct seed s1, s2, node_seed;
	struct unif_state dst_shuffle;

	/* TODO Read parameters:
	 *	Prefix file name
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
	print_seed("s1", &s1);
	print_seed("s2", &s2);
	print_seed("node_seed", &node_seed);

	/* TODO Load and shuffle destinations. */
	init_unif(&dst_shuffle, s1.seeds, SEED_UINT32_N);
	printf("\n%li\n", sample_unif_0_n1(&dst_shuffle, 10));
	printf("%li\n", sample_unif_1_n(&dst_shuffle, 10));
	printf("%li\n", sample_unif_0_n(&dst_shuffle, 10));
	end_unif(&dst_shuffle);

	/* TODO Convert destinations to a binary vector. */

	/* TODO Cache Zipf sampling. */

	/* TODO Packet template. */
	/* TODO Loop: */
		/* TODO Sample destination and update packet template. */
		/* TODO Send packet out. */

	return 0;
}
