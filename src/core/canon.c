#include "canon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool etb_append(char **buffer, size_t *size, size_t *capacity,
                       const char *text) {
  size_t needed = strlen(text);
  char *grown;
  if (*size + needed + 1U > *capacity) {
    *capacity = *capacity == 0U ? 64U : *capacity * 2U;
    while (*size + needed + 1U > *capacity) {
      *capacity *= 2U;
    }
    grown = (char *)realloc(*buffer, *capacity);
    if (grown == NULL) {
      return false;
    }
    *buffer = grown;
  }
  memcpy(*buffer + *size, text, needed);
  *size += needed;
  (*buffer)[*size] = '\0';
  return true;
}

bool etb_term_canonical_text(const etb_term *term, char **text) {
  char buffer[64];
  switch (term->kind) {
    case ETB_TERM_SYMBOL:
      return (*text = strdup(term->text == NULL ? "" : term->text)) != NULL;
    case ETB_TERM_VARIABLE:
      return asprintf(text, "?%s", term->text == NULL ? "_" : term->text) >= 0;
    case ETB_TERM_STRING:
      return asprintf(text, "\"%s\"", term->text == NULL ? "" : term->text) >= 0;
    case ETB_TERM_INTEGER:
      snprintf(buffer, sizeof(buffer), "%lld", (long long)term->integer);
      return (*text = strdup(buffer)) != NULL;
    case ETB_TERM_NULL:
    default:
      return (*text = strdup("null")) != NULL;
  }
}

bool etb_atom_canonical_text(const etb_atom *atom, char **text) {
  size_t index;
  size_t size = 0U;
  size_t capacity = 0U;
  char *buffer = NULL;
  char *term_text = NULL;
  char temp[64];

  if (atom->kind == ETB_ATOM_SPEAKS_FOR) {
    if (!asprintf(text, "%s speaks_for %s[%s|%llu|%u]", atom->principal,
                  atom->delegate, atom->delegation.scope == NULL ? "*" : atom->delegation.scope,
                  (unsigned long long)atom->delegation.expires_at,
                  atom->delegation.max_depth)) {
      return false;
    }
    return true;
  }
  if (atom->kind == ETB_ATOM_SAYS) {
    if (!etb_append(&buffer, &size, &capacity, atom->principal) ||
        !etb_append(&buffer, &size, &capacity, " says ")) {
      free(buffer);
      return false;
    }
  }
  if (!etb_append(&buffer, &size, &capacity, atom->predicate)) {
    free(buffer);
    return false;
  }
  if (atom->terms.count > 0U) {
    if (!etb_append(&buffer, &size, &capacity, "(")) {
      free(buffer);
      return false;
    }
    for (index = 0U; index < atom->terms.count; ++index) {
      if (!etb_term_canonical_text(&atom->terms.items[index], &term_text) ||
          !etb_append(&buffer, &size, &capacity, term_text)) {
        free(term_text);
        free(buffer);
        return false;
      }
      free(term_text);
      term_text = NULL;
      if (index + 1U != atom->terms.count &&
          !etb_append(&buffer, &size, &capacity, ",")) {
        free(buffer);
        return false;
      }
    }
    if (!etb_append(&buffer, &size, &capacity, ")")) {
      free(buffer);
      return false;
    }
  }
  if (atom->temporal_kind != ETB_TEMPORAL_NONE) {
    snprintf(temp, sizeof(temp), atom->temporal_kind == ETB_TEMPORAL_EXPIRES_AT
                                     ? "@%llu"
                                     : " at %llu",
             (unsigned long long)atom->temporal_value);
    if (!etb_append(&buffer, &size, &capacity, temp)) {
      free(buffer);
      return false;
    }
  }
  *text = buffer;
  return true;
}

bool etb_atom_relation_key(const etb_atom *atom, char **key) {
  if (atom->kind == ETB_ATOM_SPEAKS_FOR) {
    *key = strdup("__speaks_for__/2");
    return *key != NULL;
  }
  if (atom->kind == ETB_ATOM_SAYS) {
    return asprintf(key, "__says__/%s/%s/%zu", atom->principal, atom->predicate,
                    atom->terms.count) >= 0;
  }
  return asprintf(key, "%s/%zu", atom->predicate, atom->terms.count) >= 0;
}
