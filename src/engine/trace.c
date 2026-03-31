#include "etb/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/canon.h"
#include "../core/sha256.h"

static bool etb_trace_append_text(char **buffer, size_t *used, size_t *capacity,
                                  const char *text) {
  size_t text_size;
  char *grown;
  if (text == NULL) {
    return true;
  }
  text_size = strlen(text);
  if (*used + text_size + 1U > *capacity) {
    size_t next_capacity = *capacity == 0U ? 256U : *capacity;
    while (*used + text_size + 1U > next_capacity) {
      next_capacity *= 2U;
    }
    grown = (char *)realloc(*buffer, next_capacity);
    if (grown == NULL) {
      return false;
    }
    *buffer = grown;
    *capacity = next_capacity;
  }
  memcpy(*buffer + *used, text, text_size);
  *used += text_size;
  (*buffer)[*used] = '\0';
  return true;
}

static const char *etb_trace_kind_name(etb_trace_step_kind kind) {
  switch (kind) {
    case ETB_TRACE_FACT:
      return "fact";
    case ETB_TRACE_RULE:
      return "rule";
    case ETB_TRACE_CAPABILITY:
      return "capability";
    case ETB_TRACE_DELEGATION:
      return "delegation";
    default:
      return "unknown";
  }
}

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

bool etb_trace_render(const etb_trace *trace, char **text_out) {
  char *buffer = NULL;
  size_t used = 0U;
  size_t capacity = 0U;
  size_t index;
  if (text_out == NULL) {
    return false;
  }
  *text_out = NULL;
  for (index = 0U; index < trace->count; ++index) {
    const etb_trace_node *node = &trace->items[index];
    char *fact_text = NULL;
    char line[512];
    size_t premise_index;
    if (!etb_atom_canonical_text(&node->fact, &fact_text)) {
      free(buffer);
      return false;
    }
    snprintf(line, sizeof(line), "[%zu] %s r%zu %s", node->id,
             etb_trace_kind_name(node->kind), node->rule_id, fact_text);
    if (!etb_trace_append_text(&buffer, &used, &capacity, line)) {
      free(fact_text);
      free(buffer);
      return false;
    }
    free(fact_text);
    if (node->premise_count > 0U) {
      if (!etb_trace_append_text(&buffer, &used, &capacity, " <- ")) {
        free(buffer);
        return false;
      }
      for (premise_index = 0U; premise_index < node->premise_count; ++premise_index) {
        char premise_text[32];
        snprintf(premise_text, sizeof(premise_text), "%s#%zu",
                 premise_index == 0U ? "" : ",", node->premises[premise_index]);
        if (!etb_trace_append_text(&buffer, &used, &capacity, premise_text)) {
          free(buffer);
          return false;
        }
      }
    }
    if (node->evidence_digest != NULL && node->evidence_digest[0] != '\0') {
      if (!etb_trace_append_text(&buffer, &used, &capacity, " evidence=") ||
          !etb_trace_append_text(&buffer, &used, &capacity, node->evidence_digest)) {
        free(buffer);
        return false;
      }
    }
    if (!etb_trace_append_text(&buffer, &used, &capacity, "\n")) {
      free(buffer);
      return false;
    }
  }
  if (buffer == NULL) {
    buffer = strdup("");
    if (buffer == NULL) {
      return false;
    }
  }
  *text_out = buffer;
  return true;
}
