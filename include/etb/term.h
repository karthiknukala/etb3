#ifndef ETB_TERM_H
#define ETB_TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum etb_term_kind {
  ETB_TERM_SYMBOL = 0,
  ETB_TERM_VARIABLE = 1,
  ETB_TERM_INTEGER = 2,
  ETB_TERM_STRING = 3,
  ETB_TERM_NULL = 4
} etb_term_kind;

typedef struct etb_term {
  etb_term_kind kind;
  char *text;
  int64_t integer;
} etb_term;

typedef struct etb_term_list {
  etb_term *items;
  size_t count;
  size_t capacity;
} etb_term_list;

void etb_term_list_init(etb_term_list *list);
void etb_term_list_free(etb_term_list *list);
bool etb_term_list_push(etb_term_list *list, etb_term term);

etb_term etb_term_make_symbol(const char *text);
etb_term etb_term_make_variable(const char *text);
etb_term etb_term_make_string(const char *text);
etb_term etb_term_make_integer(int64_t value);
etb_term etb_term_make_null(void);
void etb_term_free(etb_term *term);
etb_term etb_term_clone(const etb_term *term);
bool etb_term_equals(const etb_term *lhs, const etb_term *rhs);
bool etb_term_is_ground(const etb_term *term);

#endif
