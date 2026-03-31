#ifndef ETB_ENGINE_INTERNAL_H
#define ETB_ENGINE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "etb/engine.h"

typedef struct etb_binding {
  char *name;
  etb_term value;
} etb_binding;

typedef struct etb_binding_set {
  etb_binding *items;
  size_t count;
  size_t capacity;
} etb_binding_set;

typedef struct etb_eval_state {
  etb_binding_set bindings;
  size_t *premise_trace_ids;
  size_t premise_count;
} etb_eval_state;

void etb_binding_set_init(etb_binding_set *set);
void etb_binding_set_free(etb_binding_set *set);
bool etb_binding_set_put(etb_binding_set *set, const char *name,
                         const etb_term *value);
const etb_term *etb_binding_set_get(const etb_binding_set *set, const char *name);
bool etb_binding_set_clone_into(etb_binding_set *dst, const etb_binding_set *src);

void etb_eval_state_init(etb_eval_state *state);
void etb_eval_state_free(etb_eval_state *state);
bool etb_eval_state_clone(etb_eval_state *dst, const etb_eval_state *src);
bool etb_eval_state_push_premise(etb_eval_state *state, size_t trace_id);

bool etb_unify_term(etb_binding_set *bindings, const etb_term *pattern,
                    const etb_term *candidate);
bool etb_unify_atom(etb_binding_set *bindings, const etb_atom *pattern,
                    const etb_atom *candidate, bool ignore_speaker);
bool etb_instantiate_atom(const etb_atom *pattern, const etb_binding_set *bindings,
                          etb_atom *out);

const etb_relation *etb_relation_set_find(const etb_relation_set *set,
                                          const char *key);
etb_relation *etb_relation_set_find_mut(etb_relation_set *set, const char *key);
bool etb_relation_set_add_fact(etb_relation_set *set, const etb_fact *fact,
                               char *error, size_t error_size);
void etb_relation_set_free(etb_relation_set *set);

bool etb_program_validate(const etb_engine *engine, int **strata_out,
                          char *error, size_t error_size);
bool etb_literal_is_capability(const etb_engine *engine, const etb_literal *literal);
bool etb_negated_literal_allowed(const etb_engine *engine,
                                 const etb_literal *literal);
bool etb_delegation_allows(const etb_engine *engine, const char *speaker,
                           const char *target, const char *predicate,
                           uint64_t now);

#endif
