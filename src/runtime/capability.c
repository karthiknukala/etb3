#include "etb/capability.h"

#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../core/cbor.h"

extern char **environ;

static bool etb_registry_reserve(etb_capability_registry *registry,
                                 size_t capacity) {
  etb_capability_decl *items =
      (etb_capability_decl *)realloc(registry->items, sizeof(*items) * capacity);
  if (items == NULL) {
    return false;
  }
  registry->items = items;
  registry->capacity = capacity;
  return true;
}

void etb_capability_registry_init(etb_capability_registry *registry) {
  memset(registry, 0, sizeof(*registry));
}

void etb_capability_registry_free(etb_capability_registry *registry) {
  size_t index;
  for (index = 0U; index < registry->count; ++index) {
    free(registry->items[index].name);
    free(registry->items[index].path);
  }
  free(registry->items);
  memset(registry, 0, sizeof(*registry));
}

bool etb_capability_registry_add(etb_capability_registry *registry,
                                 const etb_capability_decl *decl) {
  etb_capability_decl *slot;
  if (registry->count == registry->capacity &&
      !etb_registry_reserve(registry,
                            registry->capacity == 0U ? 4U : registry->capacity * 2U)) {
    return false;
  }
  slot = &registry->items[registry->count++];
  memset(slot, 0, sizeof(*slot));
  slot->name = decl->name == NULL ? NULL : strdup(decl->name);
  slot->path = decl->path == NULL ? NULL : strdup(decl->path);
  slot->arity = decl->arity;
  slot->deterministic = decl->deterministic;
  slot->proof_admissible = decl->proof_admissible;
  slot->timeout_ms = decl->timeout_ms;
  return slot->name != NULL;
}

const etb_capability_decl *etb_capability_registry_find(
    const etb_capability_registry *registry, const char *name, size_t arity) {
  size_t index;
  for (index = 0U; index < registry->count; ++index) {
    if (registry->items[index].arity == arity &&
        strcmp(registry->items[index].name, name) == 0) {
      return &registry->items[index];
    }
  }
  return NULL;
}

void etb_capability_result_init(etb_capability_result *result) {
  memset(result, 0, sizeof(*result));
}

void etb_capability_result_free(etb_capability_result *result) {
  size_t outer;
  size_t inner;
  if (result == NULL) {
    return;
  }
  for (outer = 0U; outer < result->tuple_count; ++outer) {
    for (inner = 0U; inner < result->tuples[outer].count; ++inner) {
      etb_term_free(&result->tuples[outer].items[inner]);
    }
    free(result->tuples[outer].items);
  }
  for (outer = 0U; outer < result->evidence_count; ++outer) {
    free(result->evidence_digests[outer]);
  }
  free(result->tuples);
  free(result->evidence_digests);
  memset(result, 0, sizeof(*result));
}

static bool etb_write_term(etb_cbor_buffer *buffer, const etb_term *term) {
  if (!etb_cbor_write_array_header(buffer, 2U) ||
      !etb_cbor_write_uint(buffer, (uint64_t)term->kind)) {
    return false;
  }
  switch (term->kind) {
    case ETB_TERM_INTEGER:
      return etb_cbor_write_int(buffer, term->integer);
    case ETB_TERM_NULL:
      return etb_cbor_write_null(buffer);
    default:
      return etb_cbor_write_text(buffer, term->text == NULL ? "" : term->text);
  }
}

static bool etb_read_term(etb_cbor_cursor *cursor, etb_term *term) {
  size_t count;
  uint64_t kind;
  char *text = NULL;
  int64_t integer;
  if (!etb_cbor_read_array_header(cursor, &count) || count != 2U ||
      !etb_cbor_read_uint(cursor, &kind)) {
    return false;
  }
  switch ((etb_term_kind)kind) {
    case ETB_TERM_INTEGER:
      if (!etb_cbor_read_int(cursor, &integer)) {
        return false;
      }
      *term = etb_term_make_integer(integer);
      return true;
    case ETB_TERM_NULL:
      if (!etb_cbor_read_null(cursor)) {
        return false;
      }
      *term = etb_term_make_null();
      return true;
    case ETB_TERM_VARIABLE:
      if (!etb_cbor_read_text(cursor, &text)) {
        return false;
      }
      *term = etb_term_make_variable(text);
      free(text);
      return true;
    case ETB_TERM_STRING:
      if (!etb_cbor_read_text(cursor, &text)) {
        return false;
      }
      *term = etb_term_make_string(text);
      free(text);
      return true;
    case ETB_TERM_SYMBOL:
    default:
      if (!etb_cbor_read_text(cursor, &text)) {
        return false;
      }
      *term = etb_term_make_symbol(text);
      free(text);
      return true;
  }
}

static bool etb_write_request(const etb_term_list *args, const bool *bound_mask,
                              etb_cbor_buffer *buffer) {
  size_t index;
  if (!etb_cbor_write_array_header(buffer, 2U) ||
      !etb_cbor_write_array_header(buffer, args->count)) {
    return false;
  }
  for (index = 0U; index < args->count; ++index) {
    if (!etb_write_term(buffer, &args->items[index])) {
      return false;
    }
  }
  if (!etb_cbor_write_array_header(buffer, args->count)) {
    return false;
  }
  for (index = 0U; index < args->count; ++index) {
    if (!etb_cbor_write_bool(buffer, bound_mask[index])) {
      return false;
    }
  }
  return true;
}

static bool etb_read_response(const unsigned char *data, size_t size,
                              etb_capability_result *result, char *error,
                              size_t error_size) {
  etb_cbor_cursor cursor;
  size_t count;
  size_t tuple_count;
  size_t tuple_index;
  size_t term_count;
  size_t term_index;
  size_t evidence_count;

  etb_cbor_cursor_init(&cursor, data, size);
  if (!etb_cbor_read_array_header(&cursor, &count) || count != 2U ||
      !etb_cbor_read_array_header(&cursor, &tuple_count)) {
    snprintf(error, error_size, "invalid adapter response");
    return false;
  }
  result->tuples = (etb_term_list *)calloc(tuple_count, sizeof(etb_term_list));
  if (tuple_count > 0U && result->tuples == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  result->tuple_count = tuple_count;
  for (tuple_index = 0U; tuple_index < tuple_count; ++tuple_index) {
    etb_term_list_init(&result->tuples[tuple_index]);
    if (!etb_cbor_read_array_header(&cursor, &term_count)) {
      snprintf(error, error_size, "invalid tuple in adapter response");
      return false;
    }
    for (term_index = 0U; term_index < term_count; ++term_index) {
      etb_term term;
      if (!etb_read_term(&cursor, &term) ||
          !etb_term_list_push(&result->tuples[tuple_index], term)) {
        snprintf(error, error_size, "invalid term in adapter response");
        return false;
      }
    }
  }
  if (!etb_cbor_read_array_header(&cursor, &evidence_count)) {
    snprintf(error, error_size, "invalid evidence list");
    return false;
  }
  result->evidence_digests =
      (char **)calloc(evidence_count, sizeof(char *));
  if (evidence_count > 0U && result->evidence_digests == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  result->evidence_count = evidence_count;
  for (tuple_index = 0U; tuple_index < evidence_count; ++tuple_index) {
    if (!etb_cbor_read_text(&cursor, &result->evidence_digests[tuple_index])) {
      snprintf(error, error_size, "invalid evidence digest");
      return false;
    }
  }
  return true;
}

static bool etb_read_all(int fd, unsigned char **data, size_t *size) {
  unsigned char *buffer = NULL;
  size_t capacity = 0U;
  size_t used = 0U;
  ssize_t count;
  for (;;) {
    if (used + 256U > capacity) {
      unsigned char *grown;
      capacity = capacity == 0U ? 256U : capacity * 2U;
      grown = (unsigned char *)realloc(buffer, capacity);
      if (grown == NULL) {
        free(buffer);
        return false;
      }
      buffer = grown;
    }
    count = read(fd, buffer + used, capacity - used);
    if (count < 0) {
      free(buffer);
      return false;
    }
    if (count == 0) {
      break;
    }
    used += (size_t)count;
  }
  *data = buffer;
  *size = used;
  return true;
}

bool etb_capability_invoke(const etb_capability_decl *decl,
                           const etb_term_list *args,
                           const bool *bound_mask,
                           etb_capability_result *result,
                           char *error,
                           size_t error_size) {
  int stdin_pipe[2];
  int stdout_pipe[2];
  posix_spawn_file_actions_t actions;
  pid_t pid;
  int status;
  etb_cbor_buffer request;
  unsigned char *response_data = NULL;
  size_t response_size = 0U;
  bool ok = false;

  etb_capability_result_init(result);
  if (decl->path == NULL) {
    snprintf(error, error_size, "capability %s has no path", decl->name);
    return false;
  }
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    snprintf(error, error_size, "failed to create pipes");
    return false;
  }
  etb_cbor_buffer_init(&request);
  if (!etb_write_request(args, bound_mask, &request)) {
    snprintf(error, error_size, "failed to encode capability request");
    goto cleanup;
  }

  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
  posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
  if (posix_spawn(&pid, decl->path, &actions, NULL, (char *const[]){decl->path, NULL},
                  environ) != 0) {
    snprintf(error, error_size, "failed to spawn adapter %s", decl->path);
    posix_spawn_file_actions_destroy(&actions);
    goto cleanup;
  }
  posix_spawn_file_actions_destroy(&actions);
  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  if (write(stdin_pipe[1], request.data, request.size) < 0) {
    snprintf(error, error_size, "failed to write capability request");
    goto cleanup;
  }
  close(stdin_pipe[1]);
  stdin_pipe[1] = -1;
  if (!etb_read_all(stdout_pipe[0], &response_data, &response_size)) {
    snprintf(error, error_size, "failed to read capability response");
    goto cleanup;
  }
  close(stdout_pipe[0]);
  stdout_pipe[0] = -1;
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) {
    snprintf(error, error_size, "adapter exited unsuccessfully");
    goto cleanup;
  }
  if (!etb_read_response(response_data, response_size, result, error,
                         error_size)) {
    goto cleanup;
  }
  ok = true;

cleanup:
  if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
  if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
  if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
  if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
  etb_cbor_buffer_free(&request);
  free(response_data);
  if (!ok) {
    etb_capability_result_free(result);
  }
  return ok;
}
