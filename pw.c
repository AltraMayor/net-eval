/* Packet writer. */

#include <stdio.h>
#include <err.h>

#include <seeds.h>
#include <rdist.h>
#include <strarray.h>
#include <sndpkt.h>

int main(void)
{
	const char *prefix_filename = "prefix-2013-03-17-14-00";
	double s = 1.0;

	const char *stack = "ip";
	const char *ifname = "eth0";
	const unsigned char dst_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	const char *dst_addr_type = "ip";
	int packet_len = 64;

	int nnodes = 3;
	int node_id = 2;
	int run = 1;
	
	struct seed s1, s2, node_seed;
	union net_addr *addrs;
	uint64_t addrs_count;
	struct zipf_cache zcache;
	struct sndpkt_engine engine;

	/* TODO Read parameters:
	 *	Prefix file name
	 *	The s parameter of Zipf distribution
	 *	Stack ("ip" or "xia")
	 *	Network interface (e.g. "eth0")
	 *	Ethernet address of router (e.g. "11:22:33:44:55:66")
	 *	Destination address template ("ip", "fb0", ..., "fb3", "via")
	 *	Packet length
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

	/* Seed @s2 is not used by PW. */

	/* Load and shuffle destination addresses. */
	addrs = load_file_as_shuffled_addrs(prefix_filename, &addrs_count,
		s1.seeds, SEED_UINT32_N);
	if (!addrs_count)
		err(1, "Prefix file `%s' is empty", prefix_filename);

	/* Cache Zipf sampling. */
	printf("Initializing Zipf cache... ");
	fflush(stdout);
	init_zipf_cache(&zcache, addrs_count * 30, s, addrs_count,
		node_seed.seeds, SEED_UINT32_N);
	printf("DONE\n");
	/*
	print_zipf_cache(&zcache);
	*/

	init_sndpkt_engine(&engine, stack, ifname, packet_len,
		dst_mac, sizeof(dst_mac), dst_addr_type);
	while (1) {
		/* Sample destination and send packet out. */
		long i = sample_zipf_cache(&zcache);
		sndpkt_send(&engine, &addrs[i - 1]);
	}

	end_sndpkt_engine(&engine);
	end_zipf_cache(&zcache);
	free_net_addr(addrs);
	return 0;
}
