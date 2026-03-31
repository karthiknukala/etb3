#include "engine_internal.h"

#include <stdlib.h>
#include <string.h>

static char *etb_strdup(const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, text, length + 1U);
  return copy;
}

void etb_binding_set_init(etb_binding_set *set) {
  memset(set, 0, sizeof(*set));
}

void etb_binding_set_free(etb_binding_set *set) {
  size_t index;
  if (set == NULL) {
    return;
  }
  for (index = 0U; index < set->count; ++index) {
    free(set->items[index].name);
    etb_term_free(&set->items[index].value);
  }
  free(set->items);
  memset(set, 0, sizeof(*set));
}

const etb_term *etb_binding_set_get(const etb_binding_set *set, const char *name) {
  size_t index;
  for (index = 0U; index < set->count; ++index) {
    if (strcmp(set->items[index].name, name) == 0) {
      return &set->items[index].value;
    }
  }
  return NULL;
}

bool etb_binding_set_put(etb_binding_set *set, const char *name,
                         const etb_term *value) {
  etb_binding *items;
  size_t capacity;
  const etb_term *existing = etb_binding_set_get(set, name);
  size_t index;
  if (existing != NULL) {
    return etb_term_equals(existing, value);
  }
  if (set->count == set->capacity) {
    capacity = set->capacity == 0U ? 8U : set->capacity * 2U;
    items = (etb_binding *)realloc(set->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return false;
    }
    set->items = items;
    set->capacity = capacity;
  }
  index = set->count++;
  set->items[index].name = etb_strdup(name);
  set->items[index].value = etb_term_clone(value);
  return set->items[index].name != NULL;
}

bool etb_binding_set_clone_into(etb_binding_set *dst, const etb_binding_set *src) {
  size_t index;
  etb_binding_set_init(dst);
  for (index = 0U; index < src->count; ++index) {
    if (!etb_binding_set_put(dst, src->items[index].name, &src->items[index].value)) {
      etb_binding_set_free(dst);
      return false;
    }
  }
  return true;
}

void etb_eval_state_init(etb_eval_state *state) {
  memset(state, 0, sizeof(*state));
  etb_binding_set_init(&state->bindings);
}

void etb_eval_state_free(etb_eval_state *state) {
  etb_binding_set_free(&state->bindings);
  free(state->premise_trace_ids);
  memset(state, 0, sizeof(*state));
}

bool etb_eval_state_clone(etb_eval_state *dst, const etb_eval_state *src) {
  etb_eval_state_init(dst);
  if (!etb_binding_set_clone_into(&dst->bindings, &src->bindings)) {
    etb_eval_state_free(dst);
    return false;
  }
  if (src->premise_count > 0U) {
    dst->premise_trace_ids =
        (size_t *)malloc(sizeof(size_t) * src->premise_count);
    if (dst->premise_trace_ids == NULL) {
      etb_eval_state_free(dst);
      return false;
    }
    memcpy(dst->premise_trace_ids, src->premise_trace_ids,
           sizeof(size_t) * src->premise_count);
    dst->premise_count = src->premise_count;
  }
  return true;
}

bool etb_eval_state_push_premise(etb_eval_state *state, size_t trace_id) {
  size_t *grown = (size_t *)realloc(
      state->premise_trace_ids, sizeof(size_t) * (state->premise_count + 1U));
  if (grown == NULL) {
    return false;
  }
  state->premise_trace_ids = grown;
  state->premise_trace_ids[state->premise_count++] = trace_id;
  return true;
}

bool etb_unify_term(etb_binding_set *bindings, const etb_term *pattern,
                    const etb_term *candidate) {
  const etb_term *bound;
  if (pattern->kind == ETB_TERM_VARIABLE) {
    bound = etb_binding_set_get(bindings, pattern->text);
    if (bound != NULL) {
      return etb_term_equals(bound, candidate);
    }
    return etb_binding_set_put(bindings, pattern->text, candidate);
  }
  if (candidate->kind == ETB_TERM_VARIABLE) {
    bound = etb_binding_set_get(bindings, candidate->text);
    if (bound != NULL) {
      return etb_term_equals(pattern, bound);
    }
    return etb_binding_set_put(bindings, candidate->text, pattern);
  }
  return etb_term_equals(pattern, candidate);
}

bool etb_unify_atom(etb_binding_set *bindings, const etb_atom *pattern,
                    const etb_atom *candidate, bool ignore_speaker) {
  size_t index;
  if (pattern->kind != candidate->kind) {
    return false;
  }
  if (pattern->kind == ETB_ATOM_SPEAKS_FOR) {
    return strcmp(pattern->principal, candidate->principal) == 0 &&
           strcmp(pattern->delegate, candidate->delegate) == 0;
  }
  if (!ignore_speaker && pattern->kind == ETB_ATOM_SAYS &&
      strcmp(pattern->principal, candidate->principal) != 0) {
    return false;
  }
  if (strcmp(pattern->predicate, candidate->predicate) != 0 ||
      pattern->terms.count != candidate->terms.count) {
    return false;
  }
  for (index = 0U; index < pattern->terms.count; ++index) {
    if (!etb_unify_term(bindings, &pattern->terms.items[index],
                        &candidate->terms.items[index])) {
      return false;
    }
  }
  return true;
}

bool etb_instantiate_atom(const etb_atom *pattern, const etb_binding_set *bindings,
                          etb_atom *out) {
  size_t index;
  etb_atom_init(out);
  out->kind = pattern->kind;
  out->principal = pattern->principal == NULL ? NULL : etb_strdup(pattern->principal);
  out->delegate = pattern->delegate == NULL ? NULL : etb_strdup(pattern->delegate);
  out->predicate = pattern->predicate == NULL ? NULL : etb_strdup(pattern->predicate);
  out->temporal_kind = pattern->temporal_kind;
  out->temporal_value = pattern->temporal_value;
  out->delegation.scope = pattern->delegation.scope == NULL
                              ? NULL
                              : etb_strdup(pattern->delegation.scope);
  out->delegation.expires_at = pattern->delegation.expires_at;
  out->delegation.max_depth = pattern->delegation.max_depth;
  for (index = 0U; index < pattern->terms.count; ++index) {
    const etb_term *source = &pattern->terms.items[index];
    const etb_term *bound = source->kind == ETB_TERM_VARIABLE
                                ? etb_binding_set_get(bindings, source->text)
                                : NULL;
    if (!etb_term_list_push(&out->terms,
                            bound == NULL ? etb_term_clone(source)
                                          : etb_term_clone(bound))) {
      etb_atom_free(out);
      return false;
    }
  }
  return true;
}
