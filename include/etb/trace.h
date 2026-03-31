#ifndef ETB_TRACE_H
#define ETB_TRACE_H

#include <stddef.h>
#include <stdint.h>

#include "etb/ast.h"

typedef enum etb_trace_step_kind {
  ETB_TRACE_FACT = 0,
  ETB_TRACE_RULE = 1,
  ETB_TRACE_CAPABILITY = 2,
  ETB_TRACE_DELEGATION = 3
} etb_trace_step_kind;

typedef struct etb_trace_node {
  size_t id;
  etb_trace_step_kind kind;
  size_t rule_id;
  etb_atom fact;
  size_t *premises;
  size_t premise_count;
  char *evidence_digest;
  char digest_hex[65];
} etb_trace_node;

typedef struct etb_trace {
  etb_trace_node *items;
  size_t count;
  size_t capacity;
} etb_trace;

void etb_trace_init(etb_trace *trace);
void etb_trace_free(etb_trace *trace);
size_t etb_trace_append(etb_trace *trace, const etb_trace_node *node);
bool etb_trace_digest_node(etb_trace_node *node);
bool etb_trace_render(const etb_trace *trace, char **text_out);

#endif
