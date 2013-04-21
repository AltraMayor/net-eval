#ifndef _EBT_H
#define _EBT_H

#include <stdio.h>

void ebt_add_rule(const char *ebtables, const char *stack, const char *if_name);

int ebt_socket(void);
void ebt_close(int sk);
void ebt_add_header_to_file(int sk, const char *stack, FILE *f);
void ebt_write_sample_to_file(int sk, const char *stack, FILE *f);

#endif	/* _EBT_H */
