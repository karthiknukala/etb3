#include "etb/ast.h"

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

void etb_atom_init(etb_atom *atom) {
  atom->kind = ETB_ATOM_PREDICATE;
  atom->principal = NULL;
  atom->delegate = NULL;
  atom->predicate = NULL;
  etb_term_list_init(&atom->terms);
  atom->temporal_kind = ETB_TEMPORAL_NONE;
  atom->temporal_value = 0U;
  atom->delegation.scope = NULL;
  atom->delegation.expires_at = 0U;
  atom->delegation.max_depth = 0U;
}

void etb_atom_free(etb_atom *atom) {
  if (atom == NULL) {
    return;
  }
  free(atom->principal);
  free(atom->delegate);
  free(atom->predicate);
  free(atom->delegation.scope);
  atom->principal = NULL;
  atom->delegate = NULL;
  atom->predicate = NULL;
  atom->delegation.scope = NULL;
  etb_term_list_free(&atom->terms);
}

etb_atom etb_atom_clone(const etb_atom *atom) {
  etb_atom clone;
  size_t index;

  etb_atom_init(&clone);
  clone.kind = atom->kind;
  clone.principal = etb_strdup(atom->principal);
  clone.delegate = etb_strdup(atom->delegate);
  clone.predicate = etb_strdup(atom->predicate);
  clone.temporal_kind = atom->temporal_kind;
  clone.temporal_value = atom->temporal_value;
  clone.delegation.scope = etb_strdup(atom->delegation.scope);
  clone.delegation.expires_at = atom->delegation.expires_at;
  clone.delegation.max_depth = atom->delegation.max_depth;
  for (index = 0U; index < atom->terms.count; ++index) {
    etb_term_list_push(&clone.terms, etb_term_clone(&atom->terms.items[index]));
  }
  return clone;
}

bool etb_atom_is_ground(const etb_atom *atom) {
  size_t index;
  if (atom->kind == ETB_ATOM_SPEAKS_FOR) {
    return true;
  }
  for (index = 0U; index < atom->terms.count; ++index) {
    if (!etb_term_is_ground(&atom->terms.items[index])) {
      return false;
    }
  }
  return true;
}

bool etb_atom_equals(const etb_atom *lhs, const etb_atom *rhs) {
  size_t index;
  if (lhs->kind != rhs->kind) {
    return false;
  }
  if (((lhs->principal == NULL) != (rhs->principal == NULL)) ||
      ((lhs->delegate == NULL) != (rhs->delegate == NULL)) ||
      ((lhs->predicate == NULL) != (rhs->predicate == NULL))) {
    return false;
  }
  if (lhs->principal != NULL && strcmp(lhs->principal, rhs->principal) != 0) {
    return false;
  }
  if (lhs->delegate != NULL && strcmp(lhs->delegate, rhs->delegate) != 0) {
    return false;
  }
  if (lhs->predicate != NULL && strcmp(lhs->predicate, rhs->predicate) != 0) {
    return false;
  }
  if (lhs->temporal_kind != rhs->temporal_kind ||
      lhs->temporal_value != rhs->temporal_value ||
      lhs->terms.count != rhs->terms.count) {
    return false;
  }
  if (((lhs->delegation.scope == NULL) != (rhs->delegation.scope == NULL))) {
    return false;
  }
  if (lhs->delegation.scope != NULL &&
      strcmp(lhs->delegation.scope, rhs->delegation.scope) != 0) {
    return false;
  }
  if (lhs->delegation.expires_at != rhs->delegation.expires_at ||
      lhs->delegation.max_depth != rhs->delegation.max_depth) {
    return false;
  }
  for (index = 0U; index < lhs->terms.count; ++index) {
    if (!etb_term_equals(&lhs->terms.items[index], &rhs->terms.items[index])) {
      return false;
    }
  }
  return true;
}

void etb_literal_list_init(etb_literal_list *list) {
  list->items = NULL;
  list->count = 0U;
  list->capacity = 0U;
}

void etb_literal_list_free(etb_literal_list *list) {
  size_t index;
  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; ++index) {
    etb_atom_free(&list->items[index].atom);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0U;
  list->capacity = 0U;
}

bool etb_literal_list_push(etb_literal_list *list, etb_literal literal) {
  etb_literal *items;
  size_t capacity;
  if (list->count == list->capacity) {
    capacity = list->capacity == 0U ? 4U : list->capacity * 2U;
    items = (etb_literal *)realloc(list->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return false;
    }
    list->items = items;
    list->capacity = capacity;
  }
  list->items[list->count++] = literal;
  return true;
}

void etb_program_init(etb_program *program) {
  program->items = NULL;
  program->count = 0U;
  program->capacity = 0U;
}

void etb_program_free(etb_program *program) {
  size_t index;
  if (program == NULL) {
    return;
  }
  for (index = 0U; index < program->count; ++index) {
    if (program->items[index].kind == ETB_STMT_CLAUSE) {
      etb_atom_free(&program->items[index].as.clause.head);
      etb_literal_list_free(&program->items[index].as.clause.body);
    } else {
      free(program->items[index].as.capability.name);
      free(program->items[index].as.capability.path);
    }
  }
  free(program->items);
  program->items = NULL;
  program->count = 0U;
  program->capacity = 0U;
}

bool etb_program_push(etb_program *program, etb_statement statement) {
  etb_statement *items;
  size_t capacity;
  if (program->count == program->capacity) {
    capacity = program->capacity == 0U ? 8U : program->capacity * 2U;
    items = (etb_statement *)realloc(program->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return false;
    }
    program->items = items;
    program->capacity = capacity;
  }
  program->items[program->count++] = statement;
  return true;
}
