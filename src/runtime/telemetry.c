#include "etb/telemetry.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static unsigned long long etb_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned long long)tv.tv_sec * 1000ULL +
         (unsigned long long)(tv.tv_usec / 1000);
}

const char *etb_telemetry_event_file(void) {
  const char *path = getenv("ETB_UI_EVENTS_FILE");
  if (path != NULL && path[0] != '\0') {
    return path;
  }
  return "/tmp/etb-ui-events.jsonl";
}

static bool etb_json_append_char(char *buffer, size_t capacity, size_t *used,
                                 char ch) {
  if (*used + 1U >= capacity) {
    return false;
  }
  buffer[(*used)++] = ch;
  buffer[*used] = '\0';
  return true;
}

static bool etb_json_append_text(char *buffer, size_t capacity, size_t *used,
                                 const char *text) {
  while (*text != '\0') {
    unsigned char ch = (unsigned char)*text++;
    switch (ch) {
    case '\\':
      if (*used + 2U >= capacity) {
        return false;
      }
      buffer[(*used)++] = '\\';
      buffer[(*used)++] = '\\';
      break;
    case '"':
      if (*used + 2U >= capacity) {
        return false;
      }
      buffer[(*used)++] = '\\';
      buffer[(*used)++] = '"';
      break;
    case '\n':
      if (*used + 2U >= capacity) {
        return false;
      }
      buffer[(*used)++] = '\\';
      buffer[(*used)++] = 'n';
      break;
    case '\r':
      if (*used + 2U >= capacity) {
        return false;
      }
      buffer[(*used)++] = '\\';
      buffer[(*used)++] = 'r';
      break;
    case '\t':
      if (*used + 2U >= capacity) {
        return false;
      }
      buffer[(*used)++] = '\\';
      buffer[(*used)++] = 't';
      break;
    default:
      if (ch < 0x20U) {
        char temp[7];
        int written = snprintf(temp, sizeof(temp), "\\u%04x", ch);
        size_t index;
        if (written < 0 || *used + (size_t)written >= capacity) {
          return false;
        }
        for (index = 0U; index < (size_t)written; ++index) {
          buffer[(*used)++] = temp[index];
        }
      } else {
        if (*used + 1U >= capacity) {
          return false;
        }
        buffer[(*used)++] = (char)ch;
      }
      break;
    }
  }
  buffer[*used] = '\0';
  return true;
}

static bool etb_json_append_field_separator(char *buffer, size_t capacity,
                                            size_t *used, bool *first) {
  if (!*first && !etb_json_append_char(buffer, capacity, used, ',')) {
    return false;
  }
  *first = false;
  return true;
}

static bool etb_json_append_string_field(char *buffer, size_t capacity,
                                         size_t *used, bool *first,
                                         const char *key,
                                         const char *value) {
  if (value == NULL) {
    return true;
  }
  if (!etb_json_append_field_separator(buffer, capacity, used, first) ||
      !etb_json_append_char(buffer, capacity, used, '"') ||
      !etb_json_append_text(buffer, capacity, used, key) ||
      !etb_json_append_char(buffer, capacity, used, '"') ||
      !etb_json_append_char(buffer, capacity, used, ':') ||
      !etb_json_append_char(buffer, capacity, used, '"') ||
      !etb_json_append_text(buffer, capacity, used, value) ||
      !etb_json_append_char(buffer, capacity, used, '"')) {
    return false;
  }
  return true;
}

static bool etb_json_append_bool_field(char *buffer, size_t capacity,
                                       size_t *used, bool *first,
                                       const char *key, bool value) {
  if (!etb_json_append_field_separator(buffer, capacity, used, first)) {
    return false;
  }
  return snprintf(buffer + *used, capacity - *used, "\"%s\":%s", key,
                  value ? "true" : "false") < (int)(capacity - *used) &&
         (*used += strlen(buffer + *used), true);
}

static bool etb_json_append_unsigned_field(char *buffer, size_t capacity,
                                           size_t *used, bool *first,
                                           const char *key,
                                           unsigned long long value) {
  if (!etb_json_append_field_separator(buffer, capacity, used, first)) {
    return false;
  }
  return snprintf(buffer + *used, capacity - *used, "\"%s\":%llu", key, value) <
             (int)(capacity - *used) &&
         (*used += strlen(buffer + *used), true);
}

static void etb_telemetry_write_line(const char *line) {
  const char *path = etb_telemetry_event_file();
  int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
  size_t length;
  char *record;
  if (fd < 0) {
    return;
  }
  length = strlen(line);
  record = (char *)malloc(length + 2U);
  if (record != NULL) {
    memcpy(record, line, length);
    record[length] = '\n';
    record[length + 1U] = '\0';
    (void)write(fd, record, length + 1U);
    free(record);
  }
  close(fd);
}

static void etb_telemetry_emit_common_prefix(char *buffer, size_t capacity,
                                             size_t *used, bool *first,
                                             const char *type) {
  *used = 0U;
  *first = true;
  buffer[0] = '\0';
  (void)etb_json_append_char(buffer, capacity, used, '{');
  (void)etb_json_append_string_field(buffer, capacity, used, first, "type", type);
  (void)etb_json_append_unsigned_field(buffer, capacity, used, first, "ts",
                                       etb_now_ms());
}

void etb_telemetry_emit_node_started(const char *node_id, const char *endpoint,
                                     const char *program_path, long pid) {
  char buffer[4096];
  size_t used;
  bool first;
  etb_telemetry_emit_common_prefix(buffer, sizeof(buffer), &used, &first,
                                   "node_started");
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "nodeId", node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "endpoint", endpoint);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "programPath", program_path);
  (void)etb_json_append_unsigned_field(buffer, sizeof(buffer), &used, &first,
                                       "pid", (unsigned long long)pid);
  (void)etb_json_append_char(buffer, sizeof(buffer), &used, '}');
  etb_telemetry_write_line(buffer);
}

void etb_telemetry_emit_request_received(const char *to_node_id,
                                         const char *to_endpoint,
                                         const char *from_node_id,
                                         const char *from_endpoint,
                                         const char *op,
                                         const char *query_text,
                                         const char *principal) {
  char buffer[8192];
  size_t used;
  bool first;
  etb_telemetry_emit_common_prefix(buffer, sizeof(buffer), &used, &first,
                                   "request_received");
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "toNodeId", to_node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "toEndpoint", to_endpoint);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "fromNodeId", from_node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "fromEndpoint", from_endpoint);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "op", op);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "query", query_text);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "principal", principal);
  (void)etb_json_append_char(buffer, sizeof(buffer), &used, '}');
  etb_telemetry_write_line(buffer);
}

void etb_telemetry_emit_query_started(const char *node_id,
                                      const char *query_text) {
  char buffer[4096];
  size_t used;
  bool first;
  etb_telemetry_emit_common_prefix(buffer, sizeof(buffer), &used, &first,
                                   "query_started");
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "nodeId", node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "query", query_text);
  (void)etb_json_append_char(buffer, sizeof(buffer), &used, '}');
  etb_telemetry_write_line(buffer);
}

void etb_telemetry_emit_query_finished(const char *node_id,
                                       const char *query_text, bool success,
                                       size_t answer_count,
                                       size_t bundle_count,
                                       const char *error_text) {
  char buffer[4096];
  size_t used;
  bool first;
  etb_telemetry_emit_common_prefix(buffer, sizeof(buffer), &used, &first,
                                   "query_finished");
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "nodeId", node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "query", query_text);
  (void)etb_json_append_bool_field(buffer, sizeof(buffer), &used, &first,
                                   "success", success);
  (void)etb_json_append_unsigned_field(buffer, sizeof(buffer), &used, &first,
                                       "answerCount",
                                       (unsigned long long)answer_count);
  (void)etb_json_append_unsigned_field(buffer, sizeof(buffer), &used, &first,
                                       "bundleCount",
                                       (unsigned long long)bundle_count);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "error", error_text);
  (void)etb_json_append_char(buffer, sizeof(buffer), &used, '}');
  etb_telemetry_write_line(buffer);
}

void etb_telemetry_emit_bundle_imported(const char *node_id,
                                        const char *source_node_id,
                                        const char *query_text) {
  char buffer[4096];
  size_t used;
  bool first;
  etb_telemetry_emit_common_prefix(buffer, sizeof(buffer), &used, &first,
                                   "bundle_imported");
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "nodeId", node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "sourceNodeId", source_node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "query", query_text);
  (void)etb_json_append_char(buffer, sizeof(buffer), &used, '}');
  etb_telemetry_write_line(buffer);
}

void etb_telemetry_emit_logic_invoke(const char *node_id,
                                     const char *goal_text,
                                     const char *target_principal,
                                     const char *target_endpoint) {
  char buffer[8192];
  size_t used;
  bool first;
  etb_telemetry_emit_common_prefix(buffer, sizeof(buffer), &used, &first,
                                   "logic_invoke");
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "nodeId", node_id);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "goal", goal_text);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "targetPrincipal", target_principal);
  (void)etb_json_append_string_field(buffer, sizeof(buffer), &used, &first,
                                     "targetEndpoint", target_endpoint);
  (void)etb_json_append_char(buffer, sizeof(buffer), &used, '}');
  etb_telemetry_write_line(buffer);
}
