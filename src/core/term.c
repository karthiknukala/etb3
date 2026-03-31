#include "etb/term.h"

#include <stdlib.h>
#include <string.h>

static char *etb_strdup(const char *text) {
  size_t length;
  char *copy;

  if (text == NULL) {
    return NULL;
  }
  length = strlen(text);
  copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, text, length + 1U);
  return copy;
}

void etb_term_list_init(etb_term_list *list) {
  list->items = NULL;
  list->count = 0U;
  list->capacity = 0U;
}

void etb_term_list_free(etb_term_list *list) {
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; ++index) {
    etb_term_free(&list->items[index]);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0U;
  list->capacity = 0U;
}

bool etb_term_list_push(etb_term_list *list, etb_term term) {
  etb_term *items;
  size_t capacity;

  if (list->count == list->capacity) {
    capacity = list->capacity == 0U ? 4U : list->capacity * 2U;
    items = (etb_term *)realloc(list->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return false;
    }
    list->items = items;
    list->capacity = capacity;
  }
  list->items[list->count++] = term;
  return true;
}

etb_term etb_term_make_symbol(const char *text) {
  etb_term term;
  term.kind = ETB_TERM_SYMBOL;
  term.text = etb_strdup(text);
  term.integer = 0;
  return term;
}

etb_term etb_term_make_variable(const char *text) {
  etb_term term;
  term.kind = ETB_TERM_VARIABLE;
  term.text = etb_strdup(text);
  term.integer = 0;
  return term;
}

etb_term etb_term_make_string(const char *text) {
  etb_term term;
  term.kind = ETB_TERM_STRING;
  term.text = etb_strdup(text);
  term.integer = 0;
  return term;
}

etb_term etb_term_make_integer(int64_t value) {
  etb_term term;
  term.kind = ETB_TERM_INTEGER;
  term.text = NULL;
  term.integer = value;
  return term;
}

etb_term etb_term_make_null(void) {
  etb_term term;
  term.kind = ETB_TERM_NULL;
  term.text = NULL;
  term.integer = 0;
  return term;
}

void etb_term_free(etb_term *term) {
  if (term == NULL) {
    return;
  }
  free(term->text);
  term->text = NULL;
  term->integer = 0;
  term->kind = ETB_TERM_NULL;
}

etb_term etb_term_clone(const etb_term *term) {
  etb_term clone;
  clone.kind = term->kind;
  clone.integer = term->integer;
  clone.text = etb_strdup(term->text);
  return clone;
}

bool etb_term_equals(const etb_term *lhs, const etb_term *rhs) {
  if (lhs->kind != rhs->kind) {
    return false;
  }
  if (lhs->kind == ETB_TERM_INTEGER) {
    return lhs->integer == rhs->integer;
  }
  if (lhs->text == NULL || rhs->text == NULL) {
    return lhs->text == rhs->text;
  }
  return strcmp(lhs->text, rhs->text) == 0;
}

bool etb_term_is_ground(const etb_term *term) {
  return term->kind != ETB_TERM_VARIABLE;
}
