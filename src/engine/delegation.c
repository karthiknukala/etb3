#include "engine_internal.h"

#include <stdbool.h>
#include <string.h>

static bool etb_scope_allows(const char *scope, const char *predicate) {
  if (scope == NULL || strcmp(scope, "*") == 0) {
    return true;
  }
  return strncmp(predicate, scope, strlen(scope)) == 0;
}

static bool etb_delegation_visit(const etb_engine *engine, const char *speaker,
                                 const char *target, const char *predicate,
                                 uint64_t now, size_t depth, size_t max_depth) {
  const etb_relation *relation = etb_relation_set_find(&engine->relations, "__speaks_for__/2");
  size_t index;
  if (strcmp(speaker, target) == 0) {
    return true;
  }
  if (relation == NULL || depth > max_depth) {
    return false;
  }
  for (index = 0U; index < relation->facts.count; ++index) {
    const etb_atom *atom = &relation->facts.items[index].atom;
    if (strcmp(atom->principal, speaker) != 0 ||
        strcmp(atom->delegate, target) != 0) {
      continue;
    }
    if (atom->delegation.expires_at != 0U && now > atom->delegation.expires_at) {
      continue;
    }
    if (!etb_scope_allows(atom->delegation.scope, predicate)) {
      continue;
    }
    return true;
  }
  for (index = 0U; index < relation->facts.count; ++index) {
    const etb_atom *atom = &relation->facts.items[index].atom;
    if (strcmp(atom->principal, speaker) != 0) {
      continue;
    }
    if (atom->delegation.expires_at != 0U && now > atom->delegation.expires_at) {
      continue;
    }
    if (!etb_scope_allows(atom->delegation.scope, predicate)) {
      continue;
    }
    if (etb_delegation_visit(engine, atom->delegate, target, predicate, now,
                             depth + 1U,
                             atom->delegation.max_depth == 0U
                                 ? max_depth
                                 : atom->delegation.max_depth)) {
      return true;
    }
  }
  return false;
}

bool etb_delegation_allows(const etb_engine *engine, const char *speaker,
                           const char *target, const char *predicate,
                           uint64_t now) {
  return etb_delegation_visit(engine, speaker, target, predicate, now, 0U, 8U);
}
