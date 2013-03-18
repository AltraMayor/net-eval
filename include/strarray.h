#ifndef _STRARRAY_H
#define _STRARRAY_H

char **load_file_as_array(const char *filename, uint64_t *parray_size);

void free_array(char **array);

void print_array(char **array, uint64_t size);

#endif	/* _STRARRAY_H */
