#include "etb/parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum etb_token_kind {
  TOK_EOF = 0,
  TOK_IDENT,
  TOK_STRING,
  TOK_INTEGER,
  TOK_LPAREN,
  TOK_RPAREN,
  TOK_LBRACKET,
  TOK_RBRACKET,
  TOK_COMMA,
  TOK_DOT,
  TOK_COLON_DASH,
  TOK_SLASH,
  TOK_EQUAL,
  TOK_AT
} etb_token_kind;

typedef struct etb_token {
  etb_token_kind kind;
  char *text;
  int64_t integer;
  size_t line;
} etb_token;

typedef struct etb_scanner {
  const char *source;
  size_t offset;
  size_t line;
  etb_token current;
} etb_scanner;

static char *etb_strdup_range(const char *start, size_t size) {
  char *text = (char *)malloc(size + 1U);
  if (text == NULL) {
    return NULL;
  }
  memcpy(text, start, size);
  text[size] = '\0';
  return text;
}

static void etb_token_free(etb_token *token) {
  free(token->text);
  token->text = NULL;
}

static void etb_set_error(char *error, size_t error_size, size_t line,
                          const char *message) {
  if (error != NULL && error_size > 0U) {
    (void)snprintf(error, error_size, "line %zu: %s", line, message);
  }
}

static void etb_scanner_skip_space(etb_scanner *scanner) {
  char ch;
  for (;;) {
    ch = scanner->source[scanner->offset];
    if (ch == '#') {
      while (scanner->source[scanner->offset] != '\0' &&
             scanner->source[scanner->offset] != '\n') {
        scanner->offset += 1U;
      }
      continue;
    }
    if (isspace((unsigned char)ch) == 0) {
      break;
    }
    if (ch == '\n') {
      scanner->line += 1U;
    }
    scanner->offset += 1U;
  }
}

static bool etb_ident_char(char ch) {
  return isalnum((unsigned char)ch) != 0 || ch == '_' || ch == ':' || ch == '.' ||
         ch == '-';
}

static bool etb_scanner_advance(etb_scanner *scanner, char *error,
                                size_t error_size) {
  const char *source = scanner->source;
  size_t start;
  char ch;

  etb_token_free(&scanner->current);
  scanner->current.kind = TOK_EOF;
  scanner->current.integer = 0;
  scanner->current.line = scanner->line;

  etb_scanner_skip_space(scanner);
  start = scanner->offset;
  scanner->current.line = scanner->line;
  ch = source[scanner->offset];
  if (ch == '\0') {
    scanner->current.kind = TOK_EOF;
    return true;
  }
  if (ch == '(') {
    scanner->current.kind = TOK_LPAREN;
    scanner->offset += 1U;
    return true;
  }
  if (ch == ')') {
    scanner->current.kind = TOK_RPAREN;
    scanner->offset += 1U;
    return true;
  }
  if (ch == '[') {
    scanner->current.kind = TOK_LBRACKET;
    scanner->offset += 1U;
    return true;
  }
  if (ch == ']') {
    scanner->current.kind = TOK_RBRACKET;
    scanner->offset += 1U;
    return true;
  }
  if (ch == ',') {
    scanner->current.kind = TOK_COMMA;
    scanner->offset += 1U;
    return true;
  }
  if (ch == '.') {
    scanner->current.kind = TOK_DOT;
    scanner->offset += 1U;
    return true;
  }
  if (ch == '/') {
    scanner->current.kind = TOK_SLASH;
    scanner->offset += 1U;
    return true;
  }
  if (ch == '=') {
    scanner->current.kind = TOK_EQUAL;
    scanner->offset += 1U;
    return true;
  }
  if (ch == '@') {
    scanner->current.kind = TOK_AT;
    scanner->offset += 1U;
    return true;
  }
  if (ch == ':' && source[scanner->offset + 1U] == '-') {
    scanner->current.kind = TOK_COLON_DASH;
    scanner->offset += 2U;
    return true;
  }
  if (ch == '"') {
    scanner->offset += 1U;
    start = scanner->offset;
    while (source[scanner->offset] != '\0' && source[scanner->offset] != '"') {
      if (source[scanner->offset] == '\n') {
        scanner->line += 1U;
      }
      scanner->offset += 1U;
    }
    if (source[scanner->offset] != '"') {
      etb_set_error(error, error_size, scanner->line, "unterminated string");
      return false;
    }
    scanner->current.kind = TOK_STRING;
    scanner->current.text =
        etb_strdup_range(source + start, scanner->offset - start);
    scanner->offset += 1U;
    return scanner->current.text != NULL;
  }
  if (isdigit((unsigned char)ch) != 0) {
    while (isdigit((unsigned char)source[scanner->offset]) != 0) {
      scanner->offset += 1U;
    }
    scanner->current.kind = TOK_INTEGER;
    scanner->current.text = etb_strdup_range(source + start,
                                             scanner->offset - start);
    if (scanner->current.text == NULL) {
      etb_set_error(error, error_size, scanner->line, "out of memory");
      return false;
    }
    scanner->current.integer = strtoll(scanner->current.text, NULL, 10);
    return true;
  }
  if (etb_ident_char(ch)) {
    while (etb_ident_char(source[scanner->offset])) {
      scanner->offset += 1U;
    }
    scanner->current.kind = TOK_IDENT;
    scanner->current.text =
        etb_strdup_range(source + start, scanner->offset - start);
    if (scanner->current.text == NULL) {
      etb_set_error(error, error_size, scanner->line, "out of memory");
      return false;
    }
    return true;
  }

  etb_set_error(error, error_size, scanner->line, "unexpected character");
  return false;
}

static bool etb_token_is(const etb_scanner *scanner, etb_token_kind kind,
                         const char *text) {
  if (scanner->current.kind != kind) {
    return false;
  }
  if (text == NULL) {
    return true;
  }
  return scanner->current.text != NULL && strcmp(scanner->current.text, text) == 0;
}

static bool etb_expect(etb_scanner *scanner, etb_token_kind kind,
                       const char *text, char *error, size_t error_size) {
  if (!etb_token_is(scanner, kind, text)) {
    etb_set_error(error, error_size, scanner->current.line, "unexpected token");
    return false;
  }
  return etb_scanner_advance(scanner, error, error_size);
}

static bool etb_parse_attr_block(etb_scanner *scanner, etb_atom *atom,
                                 etb_capability_decl *capability, char *error,
                                 size_t error_size) {
  char *key = NULL;

  if (!etb_token_is(scanner, TOK_LBRACKET, NULL)) {
    return true;
  }
  if (!etb_scanner_advance(scanner, error, error_size)) {
    return false;
  }
  while (!etb_token_is(scanner, TOK_RBRACKET, NULL)) {
    if (!etb_token_is(scanner, TOK_IDENT, NULL)) {
      etb_set_error(error, error_size, scanner->current.line,
                    "expected attribute key");
      return false;
    }
    key = scanner->current.text;
    scanner->current.text = NULL;
    if (!etb_scanner_advance(scanner, error, error_size) ||
        !etb_expect(scanner, TOK_EQUAL, NULL, error, error_size)) {
      free(key);
      return false;
    }
    if (capability != NULL) {
      if (strcmp(key, "path") == 0) {
        if (scanner->current.kind != TOK_STRING && scanner->current.kind != TOK_IDENT) {
          free(key);
          etb_set_error(error, error_size, scanner->current.line,
                        "expected path value");
          return false;
        }
        free(capability->path);
        capability->path = scanner->current.text;
        scanner->current.text = NULL;
      } else if (strcmp(key, "deterministic") == 0) {
        capability->deterministic = etb_token_is(scanner, TOK_IDENT, "true");
      } else if (strcmp(key, "proof_admissible") == 0) {
        capability->proof_admissible = etb_token_is(scanner, TOK_IDENT, "true");
      } else if (strcmp(key, "timeout_ms") == 0 && scanner->current.kind == TOK_INTEGER) {
        capability->timeout_ms = (uint32_t)scanner->current.integer;
      }
    } else if (atom != NULL) {
      if (strcmp(key, "scope") == 0 &&
          (scanner->current.kind == TOK_IDENT || scanner->current.kind == TOK_STRING)) {
        free(atom->delegation.scope);
        atom->delegation.scope = scanner->current.text;
        scanner->current.text = NULL;
      } else if (strcmp(key, "expires") == 0 && scanner->current.kind == TOK_INTEGER) {
        atom->delegation.expires_at = (uint64_t)scanner->current.integer;
      } else if (strcmp(key, "depth") == 0 && scanner->current.kind == TOK_INTEGER) {
        atom->delegation.max_depth = (uint32_t)scanner->current.integer;
      }
    }
    free(key);
    key = NULL;
    if (!etb_scanner_advance(scanner, error, error_size)) {
      return false;
    }
    if (etb_token_is(scanner, TOK_COMMA, NULL)) {
      if (!etb_scanner_advance(scanner, error, error_size)) {
        return false;
      }
    } else {
      break;
    }
  }
  return etb_expect(scanner, TOK_RBRACKET, NULL, error, error_size);
}

static bool etb_parse_term(etb_scanner *scanner, etb_term *term, char *error,
                           size_t error_size) {
  if (scanner->current.kind == TOK_INTEGER) {
    *term = etb_term_make_integer(scanner->current.integer);
    return etb_scanner_advance(scanner, error, error_size);
  }
  if (scanner->current.kind == TOK_STRING) {
    *term = etb_term_make_string(scanner->current.text);
    return etb_scanner_advance(scanner, error, error_size);
  }
  if (scanner->current.kind == TOK_IDENT) {
    if (strcmp(scanner->current.text, "null") == 0) {
      *term = etb_term_make_null();
    } else if (isupper((unsigned char)scanner->current.text[0]) != 0 ||
               scanner->current.text[0] == '_') {
      *term = etb_term_make_variable(scanner->current.text);
    } else {
      *term = etb_term_make_symbol(scanner->current.text);
    }
    return etb_scanner_advance(scanner, error, error_size);
  }
  etb_set_error(error, error_size, scanner->current.line, "expected term");
  return false;
}

static bool etb_parse_predicate_atom(etb_scanner *scanner, etb_atom *atom,
                                     char *error, size_t error_size) {
  etb_term term;
  etb_atom_init(atom);
  atom->kind = ETB_ATOM_PREDICATE;
  atom->predicate = scanner->current.text;
  scanner->current.text = NULL;
  if (!etb_scanner_advance(scanner, error, error_size)) {
    return false;
  }
  if (!etb_token_is(scanner, TOK_LPAREN, NULL)) {
    return true;
  }
  if (!etb_scanner_advance(scanner, error, error_size)) {
    return false;
  }
  while (!etb_token_is(scanner, TOK_RPAREN, NULL)) {
    if (!etb_parse_term(scanner, &term, error, error_size) ||
        !etb_term_list_push(&atom->terms, term)) {
      etb_set_error(error, error_size, scanner->current.line, "out of memory");
      return false;
    }
    if (etb_token_is(scanner, TOK_COMMA, NULL)) {
      if (!etb_scanner_advance(scanner, error, error_size)) {
        return false;
      }
    } else {
      break;
    }
  }
  return etb_expect(scanner, TOK_RPAREN, NULL, error, error_size);
}

static bool etb_parse_atom_core(etb_scanner *scanner, etb_atom *atom,
                                char *error, size_t error_size) {
  char *principal;

  if (!etb_token_is(scanner, TOK_IDENT, NULL)) {
    etb_set_error(error, error_size, scanner->current.line, "expected atom");
    return false;
  }
  principal = scanner->current.text;
  if (!etb_scanner_advance(scanner, error, error_size)) {
    return false;
  }
  if (etb_token_is(scanner, TOK_IDENT, "says")) {
    etb_atom base;
    if (!etb_scanner_advance(scanner, error, error_size) ||
        !etb_parse_predicate_atom(scanner, &base, error, error_size)) {
      free(principal);
      return false;
    }
    *atom = base;
    atom->kind = ETB_ATOM_SAYS;
    atom->principal = principal;
    return true;
  }
  if (etb_token_is(scanner, TOK_IDENT, "speaks_for")) {
    etb_atom_init(atom);
    atom->kind = ETB_ATOM_SPEAKS_FOR;
    atom->principal = principal;
    if (!etb_scanner_advance(scanner, error, error_size) ||
        !etb_token_is(scanner, TOK_IDENT, NULL)) {
      etb_set_error(error, error_size, scanner->current.line,
                    "expected delegate principal");
      return false;
    }
    atom->delegate = scanner->current.text;
    scanner->current.text = NULL;
    if (!etb_scanner_advance(scanner, error, error_size) ||
        !etb_parse_attr_block(scanner, atom, NULL, error, error_size)) {
      return false;
    }
    return true;
  }

  etb_atom_init(atom);
  atom->kind = ETB_ATOM_PREDICATE;
  atom->predicate = principal;
  if (etb_token_is(scanner, TOK_LPAREN, NULL)) {
    etb_term term;
    if (!etb_scanner_advance(scanner, error, error_size)) {
      return false;
    }
    while (!etb_token_is(scanner, TOK_RPAREN, NULL)) {
      if (!etb_parse_term(scanner, &term, error, error_size) ||
          !etb_term_list_push(&atom->terms, term)) {
        return false;
      }
      if (etb_token_is(scanner, TOK_COMMA, NULL)) {
        if (!etb_scanner_advance(scanner, error, error_size)) {
          return false;
        }
      } else {
        break;
      }
    }
    if (!etb_expect(scanner, TOK_RPAREN, NULL, error, error_size)) {
      return false;
    }
  }
  return true;
}

static bool etb_parse_atom(etb_scanner *scanner, etb_atom *atom, char *error,
                           size_t error_size) {
  if (!etb_parse_atom_core(scanner, atom, error, error_size)) {
    return false;
  }
  if (etb_token_is(scanner, TOK_AT, NULL)) {
    atom->temporal_kind = ETB_TEMPORAL_EXPIRES_AT;
    if (!etb_scanner_advance(scanner, error, error_size) ||
        scanner->current.kind != TOK_INTEGER) {
      etb_set_error(error, error_size, scanner->current.line,
                    "expected integer after @");
      return false;
    }
    atom->temporal_value = (uint64_t)scanner->current.integer;
    return etb_scanner_advance(scanner, error, error_size);
  }
  if (etb_token_is(scanner, TOK_IDENT, "at")) {
    atom->temporal_kind = ETB_TEMPORAL_ANCHORED_AT;
    if (!etb_scanner_advance(scanner, error, error_size) ||
        scanner->current.kind != TOK_INTEGER) {
      etb_set_error(error, error_size, scanner->current.line,
                    "expected integer after at");
      return false;
    }
    atom->temporal_value = (uint64_t)scanner->current.integer;
    return etb_scanner_advance(scanner, error, error_size);
  }
  return true;
}

static bool etb_parse_literal(etb_scanner *scanner, etb_literal *literal,
                              char *error, size_t error_size) {
  literal->negated = false;
  if (etb_token_is(scanner, TOK_IDENT, "not")) {
    literal->negated = true;
    if (!etb_scanner_advance(scanner, error, error_size)) {
      return false;
    }
  }
  return etb_parse_atom(scanner, &literal->atom, error, error_size);
}

static bool etb_parse_clause_or_fact(etb_scanner *scanner,
                                     etb_statement *statement, char *error,
                                     size_t error_size) {
  etb_literal literal;
  statement->kind = ETB_STMT_CLAUSE;
  statement->as.clause.source_line = scanner->current.line;
  etb_literal_list_init(&statement->as.clause.body);
  if (!etb_parse_atom(scanner, &statement->as.clause.head, error, error_size)) {
    return false;
  }
  if (etb_token_is(scanner, TOK_COLON_DASH, NULL)) {
    if (!etb_scanner_advance(scanner, error, error_size)) {
      return false;
    }
    while (!etb_token_is(scanner, TOK_DOT, NULL) &&
           !etb_token_is(scanner, TOK_EOF, NULL)) {
      if (!etb_parse_literal(scanner, &literal, error, error_size) ||
          !etb_literal_list_push(&statement->as.clause.body, literal)) {
        return false;
      }
      if (etb_token_is(scanner, TOK_COMMA, NULL)) {
        if (!etb_scanner_advance(scanner, error, error_size)) {
          return false;
        }
      } else {
        break;
      }
    }
  }
  return etb_expect(scanner, TOK_DOT, NULL, error, error_size);
}

static bool etb_parse_capability_decl(etb_scanner *scanner,
                                      etb_statement *statement, char *error,
                                      size_t error_size) {
  etb_capability_decl *decl = &statement->as.capability;
  statement->kind = ETB_STMT_CAPABILITY;
  decl->name = NULL;
  decl->path = NULL;
  decl->arity = 0U;
  decl->deterministic = false;
  decl->proof_admissible = false;
  decl->timeout_ms = 1000U;

  if (!etb_expect(scanner, TOK_IDENT, "capability", error, error_size) ||
      !etb_token_is(scanner, TOK_IDENT, NULL)) {
    return false;
  }
  decl->name = scanner->current.text;
  scanner->current.text = NULL;
  if (!etb_scanner_advance(scanner, error, error_size) ||
      !etb_expect(scanner, TOK_SLASH, NULL, error, error_size) ||
      scanner->current.kind != TOK_INTEGER) {
    etb_set_error(error, error_size, scanner->current.line,
                  "expected capability arity");
    return false;
  }
  decl->arity = (size_t)scanner->current.integer;
  if (!etb_scanner_advance(scanner, error, error_size) ||
      !etb_parse_attr_block(scanner, NULL, decl, error, error_size)) {
    return false;
  }
  return etb_expect(scanner, TOK_DOT, NULL, error, error_size);
}

bool etb_parse_program_text(const char *source, etb_program *program, char *error,
                            size_t error_size) {
  etb_scanner scanner;
  etb_statement statement;

  memset(&scanner, 0, sizeof(scanner));
  scanner.source = source;
  scanner.line = 1U;
  if (!etb_scanner_advance(&scanner, error, error_size)) {
    return false;
  }

  etb_program_init(program);
  while (!etb_token_is(&scanner, TOK_EOF, NULL)) {
    if (etb_token_is(&scanner, TOK_IDENT, "capability")) {
      if (!etb_parse_capability_decl(&scanner, &statement, error, error_size)) {
        etb_program_free(program);
        return false;
      }
    } else if (!etb_parse_clause_or_fact(&scanner, &statement, error, error_size)) {
      etb_program_free(program);
      return false;
    }
    if (!etb_program_push(program, statement)) {
      etb_program_free(program);
      etb_set_error(error, error_size, scanner.current.line, "out of memory");
      return false;
    }
  }
  etb_token_free(&scanner.current);
  return true;
}

bool etb_parse_file(const char *path, etb_program *program, char *error,
                    size_t error_size) {
  FILE *stream;
  char *buffer;
  long size;
  bool ok;

  stream = fopen(path, "rb");
  if (stream == NULL) {
    etb_set_error(error, error_size, 0U, "failed to open file");
    return false;
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    fclose(stream);
    etb_set_error(error, error_size, 0U, "failed to seek file");
    return false;
  }
  size = ftell(stream);
  if (size < 0) {
    fclose(stream);
    etb_set_error(error, error_size, 0U, "failed to determine file size");
    return false;
  }
  rewind(stream);
  buffer = (char *)malloc((size_t)size + 1U);
  if (buffer == NULL) {
    fclose(stream);
    etb_set_error(error, error_size, 0U, "out of memory");
    return false;
  }
  if (fread(buffer, 1U, (size_t)size, stream) != (size_t)size) {
    free(buffer);
    fclose(stream);
    etb_set_error(error, error_size, 0U, "failed to read file");
    return false;
  }
  buffer[size] = '\0';
  fclose(stream);
  ok = etb_parse_program_text(buffer, program, error, error_size);
  free(buffer);
  return ok;
}

bool etb_parse_atom_text(const char *source, etb_atom *atom, char *error,
                         size_t error_size) {
  etb_scanner scanner;
  memset(&scanner, 0, sizeof(scanner));
  scanner.source = source;
  scanner.line = 1U;
  if (!etb_scanner_advance(&scanner, error, error_size) ||
      !etb_parse_atom(&scanner, atom, error, error_size)) {
    return false;
  }
  if (etb_token_is(&scanner, TOK_DOT, NULL)) {
    if (!etb_scanner_advance(&scanner, error, error_size)) {
      return false;
    }
  }
  if (!etb_token_is(&scanner, TOK_EOF, NULL)) {
    etb_set_error(error, error_size, scanner.current.line,
                  "unexpected trailing tokens");
    etb_atom_free(atom);
    return false;
  }
  etb_token_free(&scanner.current);
  return true;
}
