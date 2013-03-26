#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <strarray.h>

/* Replace '\0' in @buf to @ch, and return the numer of lines in @buf.
 *
 * This function properly deals with the case that the last line in @buf
 * doesn't end with a '\n'.
 */
static uint64_t process_content(char *buf, uint64_t size, char ch)
{
	int empty = 1, first = 1;
	uint64_t lines = 0;
	uint64_t i;

	for (i = 0; i < size; i++) {
		switch (buf[i]) {
		case '\n':
			lines++;
			empty = 1; /* So far, the next line is empty. */
			break;

		case '\0':
			if (first) {
				warnx("File has '\\0' in its content");
				first = 0;
			}
			buf[i] = ch;
			/* Fall through. */

		default:
			empty = 0;
		}
	}

	return empty ? lines : (lines + 1);
}

/* This function properly deals with the case that the last line in @buf
 * doesn't end with a '\n'.
 */
static void map_content_to_array(char *content, char **array, uint64_t size)
{
	uint64_t index = 0;
	char *p = content;

	while (1) {
		switch (*p) {
		case '\n':
			array[index++] = content;
			*p = '\0';
			content = p + 1;
			break;

		case '\0':
			if (*content)
				array[index++] = content;
			assert(size == index);
			return;

		}
		p++;
	}
	assert(0); /* Execution should never reach here. */
}

char **load_file_as_array(const char *filename, uint64_t *parray_size)
{
	int fd;
	char **array;
	uint64_t array_size;
	off_t file_size;
	char *content;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		err(1, "Can't open file `%s'", filename);

	/* Obtain file's size. */
	file_size = lseek(fd, 0, SEEK_END);
	if (file_size < 0)
		err(1, "Can't find size of file `%s'", filename);
	assert(!lseek(fd, 0, SEEK_SET));

	/* Bring whole file into memory. */
	content = malloc(file_size + 1);
	assert(content);
	assert(read(fd, content, file_size) == file_size);
	assert(!close(fd));
	content[file_size] = '\0';

	array_size = process_content(content, file_size, '?');

	/* Allocate array. */
	if (!array_size) {
		*parray_size = 0;
		return NULL;
	}
	array = malloc((array_size + 1) * sizeof(array[0]));
	assert(array);
	array[0] = content;	/* Save all content on the first entry. */
	array++;		/* Ignore the first entry. */

	map_content_to_array(content, array, array_size);

	*parray_size = array_size;
	return array;
}

void free_array(char **array)
{
	if (!array)
		return;
	array--;
	free(array[0]);	/* Release _all_ content. */
	free(array);	/* Release pointers. */
}

void print_array(char **array, uint64_t size)
{
	uint64_t i;
	for (i = 0; i < size; i++)
		printf("%" PRIu64 ":%s\n", i, array[i]);
}

static void shuffle_array(char **array, uint64_t size,
	uint32_t *seeds, int seeds_len)
{
	struct unif_state shuffle_dist;

	if (size <= 1)
		return;

	init_unif(&shuffle_dist, seeds, seeds_len);
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

struct net_prefix *load_file_as_shuffled_addrs(const char *filename,
	uint64_t *parray_size, uint32_t *seeds, int seeds_len, int force_addr)
{
	char **prefix_array =
		load_file_as_array(filename, parray_size);
	struct net_prefix *prefix, *pp;
	uint64_t i, bytes;

	if (!(*parray_size))
		return NULL;
	shuffle_array(prefix_array, *parray_size, seeds, seeds_len);
	/*
	print_array(prefix_array, *parray_size);
	*/

	bytes = sizeof(*prefix) * (*parray_size);
	prefix = malloc(bytes);
	assert(prefix);

	/* Convert prefixes into binary addresses. */
	pp = prefix;
	for (i = 0; i < *parray_size; i++) {
		int a, b, c, d, m;
		assert(sscanf(prefix_array[i], "%i.%i.%i.%i/%i",
			&a, &b, &c, &d, &m) == 5);
		assert(0 <= a && a <= 255);
		assert(0 <= b && b <= 255);
		assert(0 <= c && c <= 255);
		assert(0 <= d && d <= 255);
		assert(8 <= m && m <= 32);

		pp->mask = m;
		if (!force_addr)
			m = 32;

		/* In order to make it an address (it's originally a prefix),
		 * and avoid multiple prefixes maching the address (IP uses
		 * longest prefix matching), one has to set the bit just
		 * after the mask.
		 */
		pp->addr.id[0] = a;
		pp->addr.id[1] =  8 <= m && m < 16 ? b | (0x80 >> (m -  8)) : b;
		pp->addr.id[2] = 16 <= m && m < 24 ? c | (0x80 >> (m - 16)) : c;
		pp->addr.id[3] = 24 <= m && m < 32 ? d | (0x80 >> (m < 32)) : d;
		memset(&pp->addr.id[4], 0, sizeof(pp->addr) - 4);

		pp++;
	}

	free_array(prefix_array);
	return prefix;
}

void free_net_prefix(struct net_prefix *prefix)
{
	free(prefix);
}

void assign_port(struct net_prefix *prefix, uint64_t array_size, int ports,
	struct unif_state *unif)
{
	uint64_t i;
	for (i = 0; i < array_size; i++)
		prefix[i].port = sample_unif_0_n1(unif, ports);
}
