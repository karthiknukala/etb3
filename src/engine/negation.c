#include "engine_internal.h"

bool etb_literal_is_capability(const etb_engine *engine, const etb_literal *literal) {
  return literal->atom.kind == ETB_ATOM_PREDICATE &&
         etb_capability_registry_find(&engine->capabilities, literal->atom.predicate,
                                      literal->atom.terms.count) != NULL;
}

bool etb_negated_literal_allowed(const etb_engine *engine,
                                 const etb_literal *literal) {
  (void)engine;
  if (!literal->negated) {
    return true;
  }
  if (literal->atom.kind != ETB_ATOM_PREDICATE) {
    return false;
  }
  return !etb_literal_is_capability(engine, literal);
}
