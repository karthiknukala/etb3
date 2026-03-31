#ifndef ETB_AST_H
#define ETB_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etb/term.h"

typedef enum etb_temporal_kind {
  ETB_TEMPORAL_NONE = 0,
  ETB_TEMPORAL_EXPIRES_AT = 1,
  ETB_TEMPORAL_ANCHORED_AT = 2
} etb_temporal_kind;

typedef enum etb_atom_kind {
  ETB_ATOM_PREDICATE = 0,
  ETB_ATOM_SAYS = 1,
  ETB_ATOM_SPEAKS_FOR = 2
} etb_atom_kind;

typedef struct etb_delegation_attrs {
  char *scope;
  uint64_t expires_at;
  uint32_t max_depth;
} etb_delegation_attrs;

typedef struct etb_atom {
  etb_atom_kind kind;
  char *principal;
  char *delegate;
  char *predicate;
  etb_term_list terms;
  etb_temporal_kind temporal_kind;
  uint64_t temporal_value;
  etb_delegation_attrs delegation;
} etb_atom;

typedef struct etb_literal {
  bool negated;
  etb_atom atom;
} etb_literal;

typedef struct etb_literal_list {
  etb_literal *items;
  size_t count;
  size_t capacity;
} etb_literal_list;

typedef enum etb_stmt_kind {
  ETB_STMT_CLAUSE = 0,
  ETB_STMT_CAPABILITY = 1
} etb_stmt_kind;

typedef struct etb_capability_decl {
  char *name;
  size_t arity;
  char *path;
  bool deterministic;
  bool proof_admissible;
  uint32_t timeout_ms;
} etb_capability_decl;

typedef struct etb_clause {
  etb_atom head;
  etb_literal_list body;
  size_t source_line;
} etb_clause;

typedef struct etb_statement {
  etb_stmt_kind kind;
  union {
    etb_clause clause;
    etb_capability_decl capability;
  } as;
} etb_statement;

typedef struct etb_program {
  etb_statement *items;
  size_t count;
  size_t capacity;
} etb_program;

void etb_atom_init(etb_atom *atom);
void etb_atom_free(etb_atom *atom);
etb_atom etb_atom_clone(const etb_atom *atom);
bool etb_atom_is_ground(const etb_atom *atom);
bool etb_atom_equals(const etb_atom *lhs, const etb_atom *rhs);

void etb_literal_list_init(etb_literal_list *list);
void etb_literal_list_free(etb_literal_list *list);
bool etb_literal_list_push(etb_literal_list *list, etb_literal literal);

void etb_program_init(etb_program *program);
void etb_program_free(etb_program *program);
bool etb_program_push(etb_program *program, etb_statement statement);

#endif
