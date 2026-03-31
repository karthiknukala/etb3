#include "engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/canon.h"

static bool etb_clone_statement(const etb_statement *src, etb_statement *dst) {
  size_t index;
  memset(dst, 0, sizeof(*dst));
  dst->kind = src->kind;
  if (src->kind == ETB_STMT_CAPABILITY) {
    dst->as.capability.name =
        src->as.capability.name == NULL ? NULL : strdup(src->as.capability.name);
    dst->as.capability.path =
        src->as.capability.path == NULL ? NULL : strdup(src->as.capability.path);
    dst->as.capability.arity = src->as.capability.arity;
    dst->as.capability.deterministic = src->as.capability.deterministic;
    dst->as.capability.proof_admissible = src->as.capability.proof_admissible;
    dst->as.capability.timeout_ms = src->as.capability.timeout_ms;
    return dst->as.capability.name != NULL;
  }
  dst->as.clause.head = etb_atom_clone(&src->as.clause.head);
  dst->as.clause.source_line = src->as.clause.source_line;
  etb_literal_list_init(&dst->as.clause.body);
  for (index = 0U; index < src->as.clause.body.count; ++index) {
    etb_literal literal;
    literal.negated = src->as.clause.body.items[index].negated;
    literal.atom = etb_atom_clone(&src->as.clause.body.items[index].atom);
    if (!etb_literal_list_push(&dst->as.clause.body, literal)) {
      return false;
    }
  }
  return true;
}

void etb_engine_init(etb_engine *engine) {
  memset(engine, 0, sizeof(*engine));
  etb_program_init(&engine->program);
  etb_capability_registry_init(&engine->capabilities);
  etb_trace_init(&engine->trace);
  engine->now = (uint64_t)time(NULL);
}

void etb_engine_free(etb_engine *engine) {
  etb_program_free(&engine->program);
  etb_capability_registry_free(&engine->capabilities);
  etb_relation_set_free(&engine->relations);
  etb_trace_free(&engine->trace);
  memset(engine, 0, sizeof(*engine));
}

bool etb_engine_load_program(etb_engine *engine, const etb_program *program,
                             char *error, size_t error_size) {
  size_t index;
  etb_program_free(&engine->program);
  etb_capability_registry_free(&engine->capabilities);
  etb_program_init(&engine->program);
  etb_capability_registry_init(&engine->capabilities);
  for (index = 0U; index < program->count; ++index) {
    etb_statement clone;
    if (!etb_clone_statement(&program->items[index], &clone) ||
        !etb_program_push(&engine->program, clone)) {
      snprintf(error, error_size, "out of memory");
      return false;
    }
    if (clone.kind == ETB_STMT_CAPABILITY &&
        !etb_capability_registry_add(&engine->capabilities, &clone.as.capability)) {
      snprintf(error, error_size, "failed to register capability");
      return false;
    }
  }
  return true;
}

static bool etb_fact_valid_now(const etb_engine *engine, const etb_atom *atom) {
  return atom->temporal_kind != ETB_TEMPORAL_EXPIRES_AT ||
         engine->now <= atom->temporal_value;
}

static bool etb_literal_temporal_matches(const etb_literal *literal,
                                         const etb_atom *fact_atom) {
  if (literal->atom.temporal_kind == ETB_TEMPORAL_NONE) {
    return true;
  }
  return literal->atom.temporal_kind == fact_atom->temporal_kind &&
         literal->atom.temporal_value == fact_atom->temporal_value;
}

static bool etb_relation_contains_fact(const etb_relation_set *set,
                                       const etb_atom *atom) {
  const etb_relation *relation;
  char *key = NULL;
  size_t index;
  if (!etb_atom_relation_key(atom, &key)) {
    return true;
  }
  relation = etb_relation_set_find(set, key);
  free(key);
  if (relation == NULL) {
    return false;
  }
  for (index = 0U; index < relation->facts.count; ++index) {
    if (etb_atom_equals(&relation->facts.items[index].atom, atom)) {
      return true;
    }
  }
  return false;
}

static bool etb_try_capability(const etb_engine *engine, const etb_literal *literal,
                               const etb_eval_state *state,
                               etb_eval_state **next_states, size_t *next_count,
                               char *error, size_t error_size) {
  etb_term_list args;
  bool *bound_mask = NULL;
  size_t index;
  etb_capability_result result;
  const etb_capability_decl *decl =
      etb_capability_registry_find(&engine->capabilities, literal->atom.predicate,
                                   literal->atom.terms.count);
  etb_term_list_init(&args);
  bound_mask = (bool *)calloc(literal->atom.terms.count == 0U ? 1U
                                                              : literal->atom.terms.count,
                              sizeof(bool));
  if (bound_mask == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  for (index = 0U; index < literal->atom.terms.count; ++index) {
    const etb_term *term = &literal->atom.terms.items[index];
    const etb_term *bound =
        term->kind == ETB_TERM_VARIABLE ? etb_binding_set_get(&state->bindings, term->text)
                                        : NULL;
    if (!etb_term_list_push(&args, bound == NULL ? etb_term_make_null()
                                                 : etb_term_clone(bound))) {
      snprintf(error, error_size, "out of memory");
      etb_term_list_free(&args);
      free(bound_mask);
      return false;
    }
    bound_mask[index] = bound != NULL || term->kind != ETB_TERM_VARIABLE;
  }
  etb_capability_result_init(&result);
  if (!etb_capability_invoke(decl, &args, bound_mask, &result, error, error_size)) {
    etb_term_list_free(&args);
    free(bound_mask);
    return false;
  }
  *next_states = (etb_eval_state *)calloc(result.tuple_count == 0U ? 1U
                                                                   : result.tuple_count,
                                          sizeof(etb_eval_state));
  if (*next_states == NULL) {
    snprintf(error, error_size, "out of memory");
    etb_capability_result_free(&result);
    etb_term_list_free(&args);
    free(bound_mask);
    return false;
  }
  *next_count = 0U;
  for (index = 0U; index < result.tuple_count; ++index) {
    etb_eval_state clone;
    etb_atom candidate;
    size_t trace_id;
    etb_trace_node node;
    memset(&node, 0, sizeof(node));
    if (!etb_eval_state_clone(&clone, state)) {
      continue;
    }
    etb_atom_init(&candidate);
    candidate.kind = ETB_ATOM_PREDICATE;
    candidate.predicate = strdup(literal->atom.predicate);
    for (size_t t = 0U; t < result.tuples[index].count; ++t) {
      etb_term_list_push(&candidate.terms, etb_term_clone(&result.tuples[index].items[t]));
    }
    if (candidate.predicate == NULL ||
        !etb_unify_atom(&clone.bindings, &literal->atom, &candidate, false)) {
      etb_atom_free(&candidate);
      etb_eval_state_free(&clone);
      continue;
    }
    node.kind = ETB_TRACE_CAPABILITY;
    node.fact = candidate;
    node.evidence_digest =
        result.evidence_count > index && result.evidence_digests[index] != NULL
            ? result.evidence_digests[index]
            : NULL;
    trace_id = etb_trace_append((etb_trace *)&engine->trace, &node);
    if (trace_id == (size_t)-1 || !etb_eval_state_push_premise(&clone, trace_id)) {
      etb_atom_free(&candidate);
      etb_eval_state_free(&clone);
      continue;
    }
    (*next_states)[(*next_count)++] = clone;
  }
  etb_capability_result_free(&result);
  etb_term_list_free(&args);
  free(bound_mask);
  return true;
}

static bool etb_match_literal(const etb_engine *engine, const etb_literal *literal,
                              const etb_eval_state *state,
                              etb_eval_state **next_states, size_t *next_count,
                              char *error, size_t error_size) {
  const etb_relation *relation;
  size_t index;
  char *key = NULL;

  *next_states = NULL;
  *next_count = 0U;
  if (etb_literal_is_capability(engine, literal)) {
    return etb_try_capability(engine, literal, state, next_states, next_count,
                              error, error_size);
  }

  if (literal->negated) {
    etb_eval_state *single = (etb_eval_state *)calloc(1U, sizeof(etb_eval_state));
    if (single == NULL) {
      snprintf(error, error_size, "out of memory");
      return false;
    }
    etb_eval_state_init(single);
    if (!etb_eval_state_clone(single, state)) {
      free(single);
      snprintf(error, error_size, "out of memory");
      return false;
    }
    if (!etb_atom_relation_key(&literal->atom, &key)) {
      snprintf(error, error_size, "out of memory");
      return false;
    }
    relation = etb_relation_set_find(&engine->relations, key);
    free(key);
    if (relation != NULL) {
      for (index = 0U; index < relation->facts.count; ++index) {
        etb_binding_set probe;
        etb_binding_set_init(&probe);
        if (!etb_binding_set_clone_into(&probe, &state->bindings)) {
          etb_binding_set_free(&probe);
          continue;
        }
        if (etb_fact_valid_now(engine, &relation->facts.items[index].atom) &&
            etb_literal_temporal_matches(literal, &relation->facts.items[index].atom) &&
            etb_unify_atom(&probe, &literal->atom, &relation->facts.items[index].atom, false)) {
          etb_binding_set_free(&probe);
          etb_eval_state_free(single);
          free(single);
          *next_states = NULL;
          *next_count = 0U;
          return true;
        }
        etb_binding_set_free(&probe);
      }
    }
    *next_states = single;
    *next_count = 1U;
    return true;
  }

  if (literal->atom.kind == ETB_ATOM_SAYS) {
    size_t relation_index;
    etb_eval_state *matches = NULL;
    size_t match_count = 0U;
    for (relation_index = 0U; relation_index < engine->relations.count; ++relation_index) {
      relation = &engine->relations.items[relation_index];
      if (strncmp(relation->key, "__says__/", 8U) != 0) {
        continue;
      }
      for (index = 0U; index < relation->facts.count; ++index) {
        etb_eval_state clone;
        const etb_atom *candidate = &relation->facts.items[index].atom;
        if (!etb_fact_valid_now(engine, candidate) ||
            !etb_literal_temporal_matches(literal, candidate)) {
          continue;
        }
        if (strcmp(candidate->predicate, literal->atom.predicate) != 0 ||
            candidate->terms.count != literal->atom.terms.count) {
          continue;
        }
        if (strcmp(candidate->principal, literal->atom.principal) != 0 &&
            !etb_delegation_allows(engine, candidate->principal,
                                   literal->atom.principal, candidate->predicate,
                                   engine->now)) {
          continue;
        }
        if (!etb_eval_state_clone(&clone, state) ||
            !etb_unify_atom(&clone.bindings, &literal->atom, candidate, true) ||
            !etb_eval_state_push_premise(&clone, relation->facts.items[index].trace_id)) {
          etb_eval_state_free(&clone);
          continue;
        }
        matches = (etb_eval_state *)realloc(matches, sizeof(*matches) * (match_count + 1U));
        if (matches == NULL) {
          snprintf(error, error_size, "out of memory");
          return false;
        }
        matches[match_count++] = clone;
      }
    }
    *next_states = matches;
    *next_count = match_count;
    return true;
  }

  if (!etb_atom_relation_key(&literal->atom, &key)) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  relation = etb_relation_set_find(&engine->relations, key);
  free(key);
  if (relation == NULL) {
    return true;
  }

  *next_states = (etb_eval_state *)calloc(relation->facts.count == 0U ? 1U
                                                                      : relation->facts.count,
                                          sizeof(etb_eval_state));
  if (*next_states == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  for (index = 0U; index < relation->facts.count; ++index) {
    etb_eval_state clone;
    if (!etb_fact_valid_now(engine, &relation->facts.items[index].atom) ||
        !etb_literal_temporal_matches(literal, &relation->facts.items[index].atom)) {
      continue;
    }
    if (!etb_eval_state_clone(&clone, state) ||
        !etb_unify_atom(&clone.bindings, &literal->atom,
                        &relation->facts.items[index].atom, false) ||
        !etb_eval_state_push_premise(&clone, relation->facts.items[index].trace_id)) {
      etb_eval_state_free(&clone);
      continue;
    }
    (*next_states)[(*next_count)++] = clone;
  }
  return true;
}

static bool etb_evaluate_clause_body(const etb_engine *engine, const etb_clause *clause,
                                     size_t body_index, const etb_eval_state *state,
                                     etb_fact_list *derived, size_t rule_id,
                                     char *error, size_t error_size) {
  if (body_index == clause->body.count) {
    etb_atom instantiated;
    etb_fact fact;
    etb_trace_node node;
    memset(&node, 0, sizeof(node));
    memset(&fact, 0, sizeof(fact));
    if (!etb_instantiate_atom(&clause->head, &state->bindings, &instantiated)) {
      snprintf(error, error_size, "failed to instantiate head");
      return false;
    }
    if (!etb_atom_is_ground(&instantiated)) {
      etb_atom_free(&instantiated);
      return true;
    }
    if (etb_relation_contains_fact(&engine->relations, &instantiated)) {
      etb_atom_free(&instantiated);
      return true;
    }
    node.kind = clause->head.kind == ETB_ATOM_SPEAKS_FOR
                    ? ETB_TRACE_DELEGATION
                    : clause->body.count == 0U ? ETB_TRACE_FACT : ETB_TRACE_RULE;
    node.rule_id = rule_id;
    node.fact = instantiated;
    node.premises = state->premise_trace_ids;
    node.premise_count = state->premise_count;
    fact.atom = etb_atom_clone(&instantiated);
    fact.trace_id = etb_trace_append((etb_trace *)&engine->trace, &node);
    fact.from_capability = false;
    if (fact.trace_id == (size_t)-1) {
      etb_atom_free(&instantiated);
      etb_atom_free(&fact.atom);
      snprintf(error, error_size, "failed to append trace");
      return false;
    }
    if (derived->count == derived->capacity) {
      etb_fact *grown;
      derived->capacity = derived->capacity == 0U ? 8U : derived->capacity * 2U;
      grown = (etb_fact *)realloc(derived->items, sizeof(*grown) * derived->capacity);
      if (grown == NULL) {
        etb_atom_free(&instantiated);
        etb_atom_free(&fact.atom);
        snprintf(error, error_size, "out of memory");
        return false;
      }
      derived->items = grown;
    }
    derived->items[derived->count++] = fact;
    etb_atom_free(&instantiated);
    return true;
  }

  {
    etb_eval_state *next_states = NULL;
    size_t next_count = 0U;
    size_t index;
    if (!etb_match_literal(engine, &clause->body.items[body_index], state, &next_states,
                           &next_count, error, error_size)) {
      return false;
    }
    for (index = 0U; index < next_count; ++index) {
      if (!etb_evaluate_clause_body(engine, clause, body_index + 1U,
                                    &next_states[index], derived, rule_id,
                                    error, error_size)) {
        while (index < next_count) {
          etb_eval_state_free(&next_states[index++]);
        }
        free(next_states);
        return false;
      }
      etb_eval_state_free(&next_states[index]);
    }
    free(next_states);
  }
  return true;
}

bool etb_engine_run_fixpoint(etb_engine *engine, char *error,
                             size_t error_size) {
  bool changed = true;
  int *strata = NULL;
  size_t clause_index;
  size_t pass_guard = 0U;

  if (!etb_program_validate(engine, &strata, error, error_size)) {
    return false;
  }
  free(strata);

  while (changed && pass_guard++ < 64U) {
    etb_fact_list derived;
    etb_fact_list_init(&derived);
    changed = false;
    for (clause_index = 0U; clause_index < engine->program.count; ++clause_index) {
      const etb_statement *statement = &engine->program.items[clause_index];
      etb_eval_state initial;
      if (statement->kind != ETB_STMT_CLAUSE) {
        continue;
      }
      etb_eval_state_init(&initial);
      if (!etb_evaluate_clause_body(engine, &statement->as.clause, 0U, &initial,
                                    &derived, clause_index, error, error_size)) {
        etb_eval_state_free(&initial);
        etb_fact_list_free(&derived);
        return false;
      }
      etb_eval_state_free(&initial);
    }
    for (clause_index = 0U; clause_index < derived.count; ++clause_index) {
      if (!etb_relation_contains_fact(&engine->relations, &derived.items[clause_index].atom)) {
        if (!etb_relation_set_add_fact(&engine->relations, &derived.items[clause_index],
                                       error, error_size)) {
          etb_fact_list_free(&derived);
          return false;
        }
        changed = true;
      }
    }
    etb_fact_list_free(&derived);
  }
  return true;
}

bool etb_engine_query(etb_engine *engine, const etb_atom *goal,
                      etb_fact_list *answers, char *error, size_t error_size) {
  etb_literal literal;
  etb_eval_state initial;
  etb_eval_state *states = NULL;
  size_t state_count = 0U;
  size_t index;
  etb_fact_list_init(answers);
  memset(&literal, 0, sizeof(literal));
  literal.atom = etb_atom_clone(goal);
  etb_eval_state_init(&initial);
  if (!etb_match_literal(engine, &literal, &initial, &states, &state_count, error,
                         error_size)) {
    etb_atom_free(&literal.atom);
    etb_eval_state_free(&initial);
    return false;
  }
  for (index = 0U; index < state_count; ++index) {
    etb_atom answer;
    etb_fact fact;
    if (!etb_instantiate_atom(goal, &states[index].bindings, &answer)) {
      continue;
    }
    fact.atom = answer;
    fact.trace_id = states[index].premise_count == 0U ? 0U
                                                      : states[index].premise_trace_ids[states[index].premise_count - 1U];
    fact.from_capability = false;
    if (answers->count == answers->capacity) {
      etb_fact *grown;
      answers->capacity = answers->capacity == 0U ? 4U : answers->capacity * 2U;
      grown = (etb_fact *)realloc(answers->items, sizeof(*grown) * answers->capacity);
      if (grown == NULL) {
        etb_atom_free(&fact.atom);
        continue;
      }
      answers->items = grown;
    }
    answers->items[answers->count++] = fact;
    etb_eval_state_free(&states[index]);
  }
  free(states);
  etb_atom_free(&literal.atom);
  etb_eval_state_free(&initial);
  return true;
}
