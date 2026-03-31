#include "engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/canon.h"

static void etb_fact_free(etb_fact *fact) { etb_atom_free(&fact->atom); }

void etb_fact_list_init(etb_fact_list *list) { memset(list, 0, sizeof(*list)); }

void etb_fact_list_free(etb_fact_list *list) {
  size_t index;
  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; ++index) {
    etb_fact_free(&list->items[index]);
  }
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static etb_relation *etb_relation_set_add_relation(etb_relation_set *set,
                                                   const char *key) {
  etb_relation *items;
  size_t capacity;
  etb_relation *relation;
  if (set->count == set->capacity) {
    capacity = set->capacity == 0U ? 8U : set->capacity * 2U;
    items = (etb_relation *)realloc(set->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return NULL;
    }
    set->items = items;
    set->capacity = capacity;
  }
  relation = &set->items[set->count++];
  memset(relation, 0, sizeof(*relation));
  relation->key = strdup(key);
  etb_fact_list_init(&relation->facts);
  return relation->key == NULL ? NULL : relation;
}

const etb_relation *etb_relation_set_find(const etb_relation_set *set,
                                          const char *key) {
  size_t index;
  for (index = 0U; index < set->count; ++index) {
    if (strcmp(set->items[index].key, key) == 0) {
      return &set->items[index];
    }
  }
  return NULL;
}

etb_relation *etb_relation_set_find_mut(etb_relation_set *set, const char *key) {
  size_t index;
  for (index = 0U; index < set->count; ++index) {
    if (strcmp(set->items[index].key, key) == 0) {
      return &set->items[index];
    }
  }
  return NULL;
}

bool etb_relation_set_add_fact(etb_relation_set *set, const etb_fact *fact,
                               char *error, size_t error_size) {
  etb_relation *relation;
  etb_fact *items;
  char *key = NULL;
  size_t capacity;
  size_t index;

  if (!etb_atom_relation_key(&fact->atom, &key)) {
    snprintf(error, error_size, "failed to allocate relation key");
    return false;
  }
  relation = etb_relation_set_find_mut(set, key);
  if (relation == NULL) {
    relation = etb_relation_set_add_relation(set, key);
  }
  free(key);
  if (relation == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  for (index = 0U; index < relation->facts.count; ++index) {
    if (etb_atom_equals(&relation->facts.items[index].atom, &fact->atom)) {
      return true;
    }
  }
  if (relation->facts.count == relation->facts.capacity) {
    capacity = relation->facts.capacity == 0U ? 8U : relation->facts.capacity * 2U;
    items = (etb_fact *)realloc(relation->facts.items, sizeof(*items) * capacity);
    if (items == NULL) {
      snprintf(error, error_size, "out of memory");
      return false;
    }
    relation->facts.items = items;
    relation->facts.capacity = capacity;
  }
  relation->facts.items[relation->facts.count].atom = etb_atom_clone(&fact->atom);
  relation->facts.items[relation->facts.count].trace_id = fact->trace_id;
  relation->facts.items[relation->facts.count].from_capability = fact->from_capability;
  relation->facts.count += 1U;
  return true;
}

void etb_relation_set_free(etb_relation_set *set) {
  size_t index;
  if (set == NULL) {
    return;
  }
  for (index = 0U; index < set->count; ++index) {
    free(set->items[index].key);
    etb_fact_list_free(&set->items[index].facts);
  }
  free(set->items);
  memset(set, 0, sizeof(*set));
}
