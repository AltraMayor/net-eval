#ifndef _EBT_H
#define _EBT_H

#include <stdio.h>

void ebt_add_rule(const char *ebtables, const char *stack, const char *if_name);

int ebt_socket(void);
void ebt_close(int sk);
void ebt_add_header_to_file(int sk, const char *stack, FILE *f);

/* Write detailed information.
 * The information pairs with the header printed by ebt_add_header_to_file().
 */
void ebt_write_sample_to_file(int sk, const char *stack, FILE *f);

struct ebt_counter;

struct ebt_counter *ebt_create_cnt(int sk, const char *stack);
void ebt_free_cnt(struct ebt_counter *cnt);

/* This function only prints rates.
 * Initialize @prv_cnt with ebt_create_cnt().
 * @prv_cnt is updated before returning, so it can be used for the next call.
 */
void ebt_write_rates_to_file(int sk, const char *stack, FILE *f,
	double delta_t, struct ebt_counter *prv_cnt);

#endif	/* _EBT_H */
