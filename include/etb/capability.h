#ifndef ETB_CAPABILITY_H
#define ETB_CAPABILITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etb/ast.h"

typedef struct etb_capability_result {
  etb_term_list *tuples;
  size_t tuple_count;
  char **evidence_digests;
  size_t evidence_count;
} etb_capability_result;

typedef struct etb_capability_registry {
  etb_capability_decl *items;
  size_t count;
  size_t capacity;
} etb_capability_registry;

void etb_capability_registry_init(etb_capability_registry *registry);
void etb_capability_registry_free(etb_capability_registry *registry);
bool etb_capability_registry_add(etb_capability_registry *registry,
                                 const etb_capability_decl *decl);
const etb_capability_decl *etb_capability_registry_find(
    const etb_capability_registry *registry, const char *name, size_t arity);

void etb_capability_result_init(etb_capability_result *result);
void etb_capability_result_free(etb_capability_result *result);
bool etb_capability_invoke(const etb_capability_decl *decl,
                           const etb_term_list *args,
                           const bool *bound_mask,
                           etb_capability_result *result,
                           char *error,
                           size_t error_size);

#endif
