#ifndef _STRARRAY_H
#define _STRARRAY_H

#include <stdint.h>
#include <net/xia.h>

char **load_file_as_array(const char *filename, uint64_t *parray_size);

void free_array(char **array);

void print_array(char **array, uint64_t size);

union net_addr {
	uint8_t		id[XIA_XID_MAX];
	uint32_t	ip;
};

union net_addr *load_file_as_shuffled_addrs(const char *filename,
	uint64_t *parray_size, uint32_t *seeds, int seeds_len);

void free_net_addr(union net_addr *addrs);

#endif	/* _STRARRAY_H */
