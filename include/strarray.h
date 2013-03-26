#ifndef _STRARRAY_H
#define _STRARRAY_H

#include <stdint.h>
#include <rdist.h>		/* struct unif_state	*/
#include <net/xia.h>

char **load_file_as_array(const char *filename, uint64_t *parray_size);

void free_array(char **array);

void print_array(char **array, uint64_t size);

union net_addr {
	uint8_t		id[XIA_XID_MAX];
	uint32_t	ip;
};

struct net_prefix {
	union net_addr	addr;
	uint8_t		mask;
	uint16_t	port;
};

struct net_prefix *load_file_as_shuffled_addrs(const char *filename,
	uint64_t *parray_size, uint32_t *seeds, int seeds_len, int force_addr);

void assign_port(struct net_prefix *prefix, uint64_t array_size, int ports,
	struct unif_state *unif);

void free_net_prefix(struct net_prefix *prefix);

#endif	/* _STRARRAY_H */
