#include "engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/canon.h"

typedef struct etb_string_set {
  char **items;
  size_t count;
  size_t capacity;
} etb_string_set;

static void etb_string_set_free(etb_string_set *set) {
  size_t index;
  for (index = 0U; index < set->count; ++index) {
    free(set->items[index]);
  }
  free(set->items);
  memset(set, 0, sizeof(*set));
}

static bool etb_string_set_add(etb_string_set *set, const char *text) {
  char **items;
  size_t capacity;
  size_t index;
  for (index = 0U; index < set->count; ++index) {
    if (strcmp(set->items[index], text) == 0) {
      return true;
    }
  }
  if (set->count == set->capacity) {
    capacity = set->capacity == 0U ? 8U : set->capacity * 2U;
    items = (char **)realloc(set->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return false;
    }
    set->items = items;
    set->capacity = capacity;
  }
  set->items[set->count] = strdup(text);
  if (set->items[set->count] == NULL) {
    return false;
  }
  set->count += 1U;
  return true;
}

static void etb_collect_atom_variables(const etb_atom *atom, etb_string_set *set) {
  size_t index;
  for (index = 0U; index < atom->terms.count; ++index) {
    if (atom->terms.items[index].kind == ETB_TERM_VARIABLE) {
      (void)etb_string_set_add(set, atom->terms.items[index].text);
    }
  }
}

static int etb_key_index(char **keys, size_t count, const char *key) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    if (strcmp(keys[index], key) == 0) {
      return (int)index;
    }
  }
  return -1;
}

bool etb_program_validate(const etb_engine *engine, int **strata_out,
                          char *error, size_t error_size) {
  char **keys = NULL;
  size_t key_count = 0U;
  size_t key_capacity = 0U;
  size_t clause_index;
  int *strata = NULL;
  size_t passes;
  bool changed;

  for (clause_index = 0U; clause_index < engine->program.count; ++clause_index) {
    const etb_statement *statement = &engine->program.items[clause_index];
    char *key = NULL;
    if (statement->kind != ETB_STMT_CLAUSE) {
      continue;
    }
    if (!etb_atom_relation_key(&statement->as.clause.head, &key)) {
      snprintf(error, error_size, "out of memory");
      goto fail;
    }
    if (etb_key_index(keys, key_count, key) < 0) {
      if (key_count == key_capacity) {
        char **grown;
        key_capacity = key_capacity == 0U ? 8U : key_capacity * 2U;
        grown = (char **)realloc(keys, sizeof(*keys) * key_capacity);
        if (grown == NULL) {
          free(key);
          snprintf(error, error_size, "out of memory");
          goto fail;
        }
        keys = grown;
      }
      keys[key_count++] = key;
    } else {
      free(key);
    }
  }
  strata = (int *)calloc(key_count == 0U ? 1U : key_count, sizeof(int));
  if (strata == NULL) {
    snprintf(error, error_size, "out of memory");
    goto fail;
  }

  for (clause_index = 0U; clause_index < engine->program.count; ++clause_index) {
    const etb_statement *statement = &engine->program.items[clause_index];
    etb_string_set positive = {0};
    etb_string_set constrained = {0};
    size_t literal_index;
    if (statement->kind != ETB_STMT_CLAUSE) {
      continue;
    }
    for (literal_index = 0U; literal_index < statement->as.clause.body.count;
         ++literal_index) {
      const etb_literal *literal = &statement->as.clause.body.items[literal_index];
      if (!etb_negated_literal_allowed(engine, literal)) {
        snprintf(error, error_size, "unsupported negated literal");
        etb_string_set_free(&positive);
        etb_string_set_free(&constrained);
        goto fail;
      }
      if (!literal->negated) {
        etb_collect_atom_variables(&literal->atom, &positive);
      } else {
        etb_collect_atom_variables(&literal->atom, &constrained);
      }
    }
    etb_collect_atom_variables(&statement->as.clause.head, &constrained);
    for (literal_index = 0U; literal_index < constrained.count; ++literal_index) {
      if (etb_key_index(positive.items, positive.count, constrained.items[literal_index]) < 0) {
        snprintf(error, error_size, "range restriction violated");
        etb_string_set_free(&positive);
        etb_string_set_free(&constrained);
        goto fail;
      }
    }
    etb_string_set_free(&positive);
    etb_string_set_free(&constrained);
  }

  for (passes = 0U; passes < key_count + 4U; ++passes) {
    changed = false;
    for (clause_index = 0U; clause_index < engine->program.count; ++clause_index) {
      const etb_statement *statement = &engine->program.items[clause_index];
      char *head_key = NULL;
      int head_index;
      size_t literal_index;
      if (statement->kind != ETB_STMT_CLAUSE) {
        continue;
      }
      if (!etb_atom_relation_key(&statement->as.clause.head, &head_key)) {
        snprintf(error, error_size, "out of memory");
        goto fail;
      }
      head_index = etb_key_index(keys, key_count, head_key);
      free(head_key);
      if (head_index < 0) {
        continue;
      }
      for (literal_index = 0U; literal_index < statement->as.clause.body.count;
           ++literal_index) {
        const etb_literal *literal = &statement->as.clause.body.items[literal_index];
        char *body_key = NULL;
        int body_index;
        int needed;
        if (etb_literal_is_capability(engine, literal)) {
          continue;
        }
        if (!etb_atom_relation_key(&literal->atom, &body_key)) {
          snprintf(error, error_size, "out of memory");
          goto fail;
        }
        body_index = etb_key_index(keys, key_count, body_key);
        free(body_key);
        if (body_index < 0) {
          continue;
        }
        needed = strata[body_index] + (literal->negated ? 1 : 0);
        if (strata[head_index] < needed) {
          strata[head_index] = needed;
          changed = true;
        }
      }
    }
    if (!changed) {
      break;
    }
  }
  free(keys);
  *strata_out = strata;
  return true;

fail:
  if (keys != NULL) {
    size_t index;
    for (index = 0U; index < key_count; ++index) {
      free(keys[index]);
    }
  }
  free(keys);
  free(strata);
  return false;
}
