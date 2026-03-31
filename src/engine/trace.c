#include "etb/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/canon.h"
#include "../core/sha256.h"

void etb_trace_init(etb_trace *trace) {
  trace->items = NULL;
  trace->count = 0U;
  trace->capacity = 0U;
}

void etb_trace_free(etb_trace *trace) {
  size_t index;
  if (trace == NULL) {
    return;
  }
  for (index = 0U; index < trace->count; ++index) {
    etb_atom_free(&trace->items[index].fact);
    free(trace->items[index].premises);
    free(trace->items[index].evidence_digest);
  }
  free(trace->items);
  trace->items = NULL;
  trace->count = 0U;
  trace->capacity = 0U;
}

bool etb_trace_digest_node(etb_trace_node *node) {
  char *atom_text = NULL;
  char *buffer = NULL;
  size_t capacity = 0U;
  size_t size = 0U;
  size_t index;

  if (!etb_atom_canonical_text(&node->fact, &atom_text)) {
    return false;
  }
  capacity = 128U + strlen(atom_text);
  buffer = (char *)malloc(capacity);
  if (buffer == NULL) {
    free(atom_text);
    return false;
  }
  size = (size_t)snprintf(buffer, capacity, "%zu|%d|%zu|%s|", node->id,
                          (int)node->kind, node->rule_id, atom_text);
  for (index = 0U; index < node->premise_count; ++index) {
    size += (size_t)snprintf(buffer + size, capacity - size, "%zu,",
                             node->premises[index]);
  }
  if (node->evidence_digest != NULL) {
    (void)snprintf(buffer + size, capacity - size, "|%s", node->evidence_digest);
  }
  etb_sha256_hex((const unsigned char *)buffer, strlen(buffer), node->digest_hex);
  free(atom_text);
  free(buffer);
  return true;
}

size_t etb_trace_append(etb_trace *trace, const etb_trace_node *node) {
  etb_trace_node *items;
  etb_trace_node *target;
  size_t capacity;
  if (trace->count == trace->capacity) {
    capacity = trace->capacity == 0U ? 8U : trace->capacity * 2U;
    items = (etb_trace_node *)realloc(trace->items, sizeof(*items) * capacity);
    if (items == NULL) {
      return (size_t)-1;
    }
    trace->items = items;
    trace->capacity = capacity;
  }
  target = &trace->items[trace->count];
  memset(target, 0, sizeof(*target));
  target->id = trace->count;
  target->kind = node->kind;
  target->rule_id = node->rule_id;
  target->fact = etb_atom_clone(&node->fact);
  if (node->premise_count > 0U) {
    target->premises = (size_t *)malloc(sizeof(size_t) * node->premise_count);
    if (target->premises == NULL) {
      etb_atom_free(&target->fact);
      return (size_t)-1;
    }
    memcpy(target->premises, node->premises, sizeof(size_t) * node->premise_count);
    target->premise_count = node->premise_count;
  }
  if (node->evidence_digest != NULL) {
    target->evidence_digest = strdup(node->evidence_digest);
  }
  if (!etb_trace_digest_node(target)) {
    return (size_t)-1;
  }
  trace->count += 1U;
  return trace->count - 1U;
}
