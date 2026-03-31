#ifndef ETB_ENGINE_H
#define ETB_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etb/ast.h"
#include "etb/capability.h"
#include "etb/cert.h"
#include "etb/parser.h"

typedef struct etb_fact {
  etb_atom atom;
  size_t trace_id;
  bool from_capability;
} etb_fact;

typedef struct etb_fact_list {
  etb_fact *items;
  size_t count;
  size_t capacity;
} etb_fact_list;

typedef struct etb_relation {
  char *key;
  etb_fact_list facts;
} etb_relation;

typedef struct etb_relation_set {
  etb_relation *items;
  size_t count;
  size_t capacity;
} etb_relation_set;

typedef struct etb_engine {
  etb_program program;
  etb_capability_registry capabilities;
  etb_relation_set relations;
  etb_trace trace;
  uint64_t now;
} etb_engine;

void etb_engine_init(etb_engine *engine);
void etb_engine_free(etb_engine *engine);
bool etb_engine_load_program(etb_engine *engine, const etb_program *program,
                             char *error, size_t error_size);
bool etb_engine_run_fixpoint(etb_engine *engine, char *error,
                             size_t error_size);
bool etb_engine_query(etb_engine *engine, const etb_atom *goal,
                      etb_fact_list *answers, char *error, size_t error_size);
bool etb_engine_import_answers(etb_engine *engine, const etb_atom *answers,
                               size_t answer_count, const char *evidence_digest,
                               char *error, size_t error_size);
void etb_fact_list_init(etb_fact_list *list);
void etb_fact_list_free(etb_fact_list *list);

#endif
