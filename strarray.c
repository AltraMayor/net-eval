#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
