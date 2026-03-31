#include "etb/distributed.h"

#include "etb/cert.h"
#include "etb/engine.h"
#include "etb/parser.h"

#include "../core/canon.h"
#include "../core/cbor.h"
#include "../engine/engine_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <spawn.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct etb_string_list {
  char **items;
  size_t count;
  size_t capacity;
} etb_string_list;

typedef struct etb_node_context {
  const char *node_id;
  const char *program_path;
  const char *listen_endpoint;
  const etb_peer_route_map *peers;
  const etb_endpoint_list *seeds;
  const char *prover_path;
  etb_peer_route_map local_routes;
  char registry_path[256];
} etb_node_context;

static void etb_string_list_init(etb_string_list *list) {
  memset(list, 0, sizeof(*list));
}

static void etb_string_list_free(etb_string_list *list) {
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    free(list->items[index]);
  }
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static bool etb_string_list_contains(const etb_string_list *list,
                                     const char *text) {
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    if (strcmp(list->items[index], text) == 0) {
      return true;
    }
  }
  return false;
}

static bool etb_string_list_add(etb_string_list *list, const char *text) {
  char **grown;
  if (etb_string_list_contains(list, text)) {
    return true;
  }
  if (list->count == list->capacity) {
    size_t capacity = list->capacity == 0U ? 8U : list->capacity * 2U;
    grown = (char **)realloc(list->items, sizeof(*grown) * capacity);
    if (grown == NULL) {
      return false;
    }
    list->items = grown;
    list->capacity = capacity;
  }
  list->items[list->count] = strdup(text);
  if (list->items[list->count] == NULL) {
    return false;
  }
  list->count += 1U;
  return true;
}

void etb_peer_route_map_init(etb_peer_route_map *map) {
  memset(map, 0, sizeof(*map));
}

void etb_peer_route_map_free(etb_peer_route_map *map) {
  size_t index;
  for (index = 0U; index < map->count; ++index) {
    free(map->items[index].principal);
    free(map->items[index].endpoint);
  }
  free(map->items);
  memset(map, 0, sizeof(*map));
}

bool etb_peer_route_map_add(etb_peer_route_map *map, const char *principal,
                            const char *endpoint) {
  etb_peer_route *grown;
  size_t index;
  for (index = 0U; index < map->count; ++index) {
    if (strcmp(map->items[index].principal, principal) == 0) {
      char *copy = strdup(endpoint);
      if (copy == NULL) {
        return false;
      }
      free(map->items[index].endpoint);
      map->items[index].endpoint = copy;
      return true;
    }
  }
  if (map->count == map->capacity) {
    size_t capacity = map->capacity == 0U ? 8U : map->capacity * 2U;
    grown = (etb_peer_route *)realloc(map->items, sizeof(*grown) * capacity);
    if (grown == NULL) {
      return false;
    }
    map->items = grown;
    map->capacity = capacity;
  }
  map->items[map->count].principal = strdup(principal);
  map->items[map->count].endpoint = strdup(endpoint);
  if (map->items[map->count].principal == NULL ||
      map->items[map->count].endpoint == NULL) {
    free(map->items[map->count].principal);
    free(map->items[map->count].endpoint);
    return false;
  }
  map->count += 1U;
  return true;
}

const char *etb_peer_route_map_lookup(const etb_peer_route_map *map,
                                      const char *principal) {
  size_t index;
  for (index = 0U; index < map->count; ++index) {
    if (strcmp(map->items[index].principal, principal) == 0) {
      return map->items[index].endpoint;
    }
  }
  return NULL;
}

bool etb_peer_route_map_merge(etb_peer_route_map *dst,
                              const etb_peer_route_map *src) {
  size_t index;
  for (index = 0U; index < src->count; ++index) {
    if (!etb_peer_route_map_add(dst, src->items[index].principal,
                                src->items[index].endpoint)) {
      return false;
    }
  }
  return true;
}

void etb_endpoint_list_init(etb_endpoint_list *list) {
  memset(list, 0, sizeof(*list));
}

void etb_endpoint_list_free(etb_endpoint_list *list) {
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    free(list->items[index]);
  }
  free(list->items);
  memset(list, 0, sizeof(*list));
}

bool etb_endpoint_list_add(etb_endpoint_list *list, const char *endpoint) {
  char **grown;
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    if (strcmp(list->items[index], endpoint) == 0) {
      return true;
    }
  }
  if (list->count == list->capacity) {
    size_t capacity = list->capacity == 0U ? 8U : list->capacity * 2U;
    grown = (char **)realloc(list->items, sizeof(*grown) * capacity);
    if (grown == NULL) {
      return false;
    }
    list->items = grown;
    list->capacity = capacity;
  }
  list->items[list->count] = strdup(endpoint);
  if (list->items[list->count] == NULL) {
    return false;
  }
  list->count += 1U;
  return true;
}

static void etb_bundle_free(etb_bundle *bundle) {
  free(bundle->node_id);
  free(bundle->query_text);
  free(bundle->certificate_bytes);
  free(bundle->proof_bytes);
  memset(bundle, 0, sizeof(*bundle));
}

void etb_bundle_list_init(etb_bundle_list *list) {
  memset(list, 0, sizeof(*list));
}

void etb_bundle_list_free(etb_bundle_list *list) {
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    etb_bundle_free(&list->items[index]);
  }
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static bool etb_bundle_matches(const etb_bundle *bundle, const char *node_id,
                               const char *query_text) {
  return bundle->node_id != NULL && bundle->query_text != NULL &&
         strcmp(bundle->node_id, node_id) == 0 &&
         strcmp(bundle->query_text, query_text) == 0;
}

bool etb_bundle_list_add_copy(etb_bundle_list *list, const char *node_id,
                              const char *query_text,
                              const unsigned char *certificate_bytes,
                              size_t certificate_size,
                              const unsigned char *proof_bytes,
                              size_t proof_size) {
  etb_bundle *grown;
  size_t index;
  for (index = 0U; index < list->count; ++index) {
    if (etb_bundle_matches(&list->items[index], node_id, query_text)) {
      return true;
    }
  }
  if (list->count == list->capacity) {
    size_t capacity = list->capacity == 0U ? 8U : list->capacity * 2U;
    grown = (etb_bundle *)realloc(list->items, sizeof(*grown) * capacity);
    if (grown == NULL) {
      return false;
    }
    list->items = grown;
    list->capacity = capacity;
  }
  memset(&list->items[list->count], 0, sizeof(list->items[list->count]));
  list->items[list->count].node_id = strdup(node_id);
  list->items[list->count].query_text = strdup(query_text);
  list->items[list->count].certificate_bytes =
      (unsigned char *)malloc(certificate_size == 0U ? 1U : certificate_size);
  if (list->items[list->count].node_id == NULL ||
      list->items[list->count].query_text == NULL ||
      (certificate_size > 0U &&
       list->items[list->count].certificate_bytes == NULL)) {
    etb_bundle_free(&list->items[list->count]);
    return false;
  }
  if (certificate_size > 0U) {
    memcpy(list->items[list->count].certificate_bytes, certificate_bytes,
           certificate_size);
  }
  list->items[list->count].certificate_size = certificate_size;
  if (proof_size > 0U) {
    list->items[list->count].proof_bytes =
        (unsigned char *)malloc(proof_size);
    if (list->items[list->count].proof_bytes == NULL) {
      etb_bundle_free(&list->items[list->count]);
      return false;
    }
    memcpy(list->items[list->count].proof_bytes, proof_bytes, proof_size);
    list->items[list->count].proof_size = proof_size;
  }
  list->count += 1U;
  return true;
}

bool etb_bundle_list_append_unique(etb_bundle_list *dst,
                                   const etb_bundle_list *src) {
  size_t index;
  for (index = 0U; index < src->count; ++index) {
    if (!etb_bundle_list_add_copy(dst, src->items[index].node_id,
                                  src->items[index].query_text,
                                  src->items[index].certificate_bytes,
                                  src->items[index].certificate_size,
                                  src->items[index].proof_bytes,
                                  src->items[index].proof_size)) {
      return false;
    }
  }
  return true;
}

static bool etb_parse_endpoint(const char *endpoint, char **host_out,
                               char **port_out) {
  const char *colon = strrchr(endpoint, ':');
  size_t host_size;
  if (colon == NULL || colon == endpoint || colon[1] == '\0') {
    return false;
  }
  host_size = (size_t)(colon - endpoint);
  *host_out = (char *)malloc(host_size + 1U);
  *port_out = strdup(colon + 1);
  if (*host_out == NULL || *port_out == NULL) {
    free(*host_out);
    free(*port_out);
    *host_out = NULL;
    *port_out = NULL;
    return false;
  }
  memcpy(*host_out, endpoint, host_size);
  (*host_out)[host_size] = '\0';
  return true;
}

static bool etb_route_is_publishable(const char *principal,
                                     const char *endpoint) {
  return principal != NULL && principal[0] != '\0' && endpoint != NULL &&
         endpoint[0] != '\0';
}

static void etb_sanitize_name(const char *input, char *output,
                              size_t output_size) {
  size_t used = 0U;
  while (*input != '\0' && used + 1U < output_size) {
    char ch = *input++;
    output[used++] =
        ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9'))
            ? ch
            : '_';
  }
  output[used] = '\0';
}

static bool etb_registry_load(const char *path, etb_peer_route_map *routes) {
  FILE *stream = fopen(path, "rb");
  char line[512];
  etb_peer_route_map_init(routes);
  if (stream == NULL) {
    return true;
  }
  while (fgets(line, sizeof(line), stream) != NULL) {
    char *sep = strchr(line, '|');
    char *newline;
    if (sep == NULL) {
      continue;
    }
    *sep = '\0';
    newline = strchr(sep + 1, '\n');
    if (newline != NULL) {
      *newline = '\0';
    }
    if (!etb_route_is_publishable(line, sep + 1)) {
      continue;
    }
    if (!etb_peer_route_map_add(routes, line, sep + 1)) {
      fclose(stream);
      etb_peer_route_map_free(routes);
      return false;
    }
  }
  fclose(stream);
  return true;
}

static bool etb_registry_save(const char *path, const etb_peer_route_map *routes) {
  FILE *stream = fopen(path, "wb");
  size_t index;
  if (stream == NULL) {
    return false;
  }
  for (index = 0U; index < routes->count; ++index) {
    if (!etb_route_is_publishable(routes->items[index].principal,
                                  routes->items[index].endpoint)) {
      continue;
    }
    if (fprintf(stream, "%s|%s\n", routes->items[index].principal,
                routes->items[index].endpoint) < 0) {
      fclose(stream);
      return false;
    }
  }
  fclose(stream);
  return true;
}

static bool etb_registry_merge_and_save(const char *path,
                                        const etb_peer_route_map *extra) {
  etb_peer_route_map routes;
  bool ok;
  if (!etb_registry_load(path, &routes)) {
    return false;
  }
  ok = etb_peer_route_map_merge(&routes, extra) && etb_registry_save(path, &routes);
  etb_peer_route_map_free(&routes);
  return ok;
}

static bool etb_collect_local_routes(const char *program_path,
                                     const char *listen_endpoint,
                                     const char *node_id,
                                     etb_peer_route_map *routes,
                                     char *error, size_t error_size) {
  etb_program program;
  size_t index;
  etb_program_init(&program);
  etb_peer_route_map_init(routes);
  if (!etb_parse_file(program_path, &program, error, error_size)) {
    return false;
  }
  for (index = 0U; index < program.count; ++index) {
    if (program.items[index].kind == ETB_STMT_CLAUSE &&
        program.items[index].as.clause.head.kind == ETB_ATOM_SAYS &&
        program.items[index].as.clause.head.principal != NULL &&
        !etb_peer_route_map_add(routes,
                                program.items[index].as.clause.head.principal,
                                listen_endpoint)) {
      etb_program_free(&program);
      snprintf(error, error_size, "out of memory");
      return false;
    }
  }
  if (routes->count == 0U &&
      !etb_peer_route_map_add(routes, node_id, listen_endpoint)) {
    etb_program_free(&program);
    snprintf(error, error_size, "out of memory");
    return false;
  }
  etb_program_free(&program);
  return true;
}

static bool etb_write_all(int fd, const unsigned char *buffer, size_t size) {
  while (size > 0U) {
    ssize_t written = write(fd, buffer, size);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    buffer += (size_t)written;
    size -= (size_t)written;
  }
  return true;
}

static bool etb_read_all(int fd, unsigned char *buffer, size_t size) {
  while (size > 0U) {
    ssize_t got = read(fd, buffer, size);
    if (got == 0) {
      return false;
    }
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    buffer += (size_t)got;
    size -= (size_t)got;
  }
  return true;
}

static bool etb_send_message(int fd, const unsigned char *payload,
                             size_t payload_size) {
  unsigned char header[4];
  header[0] = (unsigned char)(payload_size >> 24U);
  header[1] = (unsigned char)(payload_size >> 16U);
  header[2] = (unsigned char)(payload_size >> 8U);
  header[3] = (unsigned char)payload_size;
  return etb_write_all(fd, header, sizeof(header)) &&
         etb_write_all(fd, payload, payload_size);
}

static bool etb_recv_message(int fd, unsigned char **payload_out,
                             size_t *payload_size_out) {
  unsigned char header[4];
  size_t size;
  unsigned char *payload;
  if (!etb_read_all(fd, header, sizeof(header))) {
    return false;
  }
  size = ((size_t)header[0] << 24U) | ((size_t)header[1] << 16U) |
         ((size_t)header[2] << 8U) | (size_t)header[3];
  payload = (unsigned char *)malloc(size == 0U ? 1U : size);
  if (size > 0U && payload == NULL) {
    return false;
  }
  if (size > 0U && !etb_read_all(fd, payload, size)) {
    free(payload);
    return false;
  }
  *payload_out = payload;
  *payload_size_out = size;
  return true;
}

static bool etb_connect_endpoint(const char *endpoint, int *fd_out,
                                 char *error, size_t error_size) {
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct addrinfo *it;
  int fd = -1;
  char *host = NULL;
  char *port = NULL;
  int status;

  if (!etb_parse_endpoint(endpoint, &host, &port)) {
    snprintf(error, error_size, "invalid endpoint '%s'", endpoint);
    return false;
  }
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  status = getaddrinfo(host, port, &hints, &result);
  if (status != 0) {
    snprintf(error, error_size, "getaddrinfo failed for %s", endpoint);
    free(host);
    free(port);
    return false;
  }
  for (it = result; it != NULL; it = it->ai_next) {
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  free(host);
  free(port);
  if (fd < 0) {
    snprintf(error, error_size, "failed to connect to %s", endpoint);
    return false;
  }
  *fd_out = fd;
  return true;
}

static bool etb_listen_endpoint(const char *endpoint, int *fd_out, char *error,
                                size_t error_size) {
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct addrinfo *it;
  int fd = -1;
  int yes = 1;
  char *host = NULL;
  char *port = NULL;
  int status;

  if (!etb_parse_endpoint(endpoint, &host, &port)) {
    snprintf(error, error_size, "invalid endpoint '%s'", endpoint);
    return false;
  }
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  status = getaddrinfo(host, port, &hints, &result);
  if (status != 0) {
    snprintf(error, error_size, "getaddrinfo failed for %s", endpoint);
    free(host);
    free(port);
    return false;
  }
  for (it = result; it != NULL; it = it->ai_next) {
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      continue;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(fd, it->ai_addr, it->ai_addrlen) == 0 && listen(fd, 16) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(result);
  free(host);
  free(port);
  if (fd < 0) {
    snprintf(error, error_size, "failed to listen on %s", endpoint);
    return false;
  }
  *fd_out = fd;
  return true;
}

static bool etb_write_temp_file(const char *suffix,
                                const unsigned char *bytes, size_t size,
                                char *path_out, size_t path_out_size,
                                char *error, size_t error_size) {
  char template_path[256];
  int fd;
  FILE *stream;
  snprintf(template_path, sizeof(template_path), "/tmp/etbXXXXXX%s", suffix);
  fd = mkstemps(template_path, (int)strlen(suffix));
  if (fd < 0) {
    snprintf(error, error_size, "failed to create temp file");
    return false;
  }
  stream = fdopen(fd, "wb");
  if (stream == NULL) {
    close(fd);
    unlink(template_path);
    snprintf(error, error_size, "failed to open temp file stream");
    return false;
  }
  if (size > 0U && fwrite(bytes, 1U, size, stream) != size) {
    fclose(stream);
    unlink(template_path);
    snprintf(error, error_size, "failed to write temp file");
    return false;
  }
  fclose(stream);
  snprintf(path_out, path_out_size, "%s", template_path);
  return true;
}

static bool etb_read_file_bytes(const char *path, unsigned char **bytes_out,
                                size_t *size_out, char *error,
                                size_t error_size) {
  FILE *stream = fopen(path, "rb");
  long size;
  unsigned char *bytes;
  if (stream == NULL) {
    snprintf(error, error_size, "failed to open %s", path);
    return false;
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    fclose(stream);
    snprintf(error, error_size, "failed to seek %s", path);
    return false;
  }
  size = ftell(stream);
  if (size < 0) {
    fclose(stream);
    snprintf(error, error_size, "failed to size %s", path);
    return false;
  }
  rewind(stream);
  bytes = (unsigned char *)malloc(size == 0L ? 1U : (size_t)size);
  if (size > 0L && bytes == NULL) {
    fclose(stream);
    snprintf(error, error_size, "out of memory");
    return false;
  }
  if (size > 0L && fread(bytes, 1U, (size_t)size, stream) != (size_t)size) {
    fclose(stream);
    free(bytes);
    snprintf(error, error_size, "failed to read %s", path);
    return false;
  }
  fclose(stream);
  *bytes_out = bytes;
  *size_out = (size_t)size;
  return true;
}

static bool etb_run_prover_command(const char *prover_path, const char *command,
                                   const char *cert_path,
                                   const char *proof_path, char *error,
                                   size_t error_size) {
  pid_t pid;
  int status;
  char *const argv[] = {(char *)prover_path, (char *)command, (char *)cert_path,
                        (char *)proof_path, NULL};
  if (posix_spawn(&pid, prover_path, NULL, NULL, argv, environ) != 0) {
    snprintf(error, error_size, "failed to launch prover %s", prover_path);
    return false;
  }
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) {
    snprintf(error, error_size, "prover command '%s' failed", command);
    return false;
  }
  return true;
}

static bool etb_prove_certificate(const char *prover_path,
                                  const unsigned char *certificate_bytes,
                                  size_t certificate_size,
                                  unsigned char **proof_bytes_out,
                                  size_t *proof_size_out, char *error,
                                  size_t error_size) {
  char cert_path[256];
  char proof_path[256];
  unsigned char *proof_bytes = NULL;
  size_t proof_size = 0U;
  int fd;

  if (!etb_write_temp_file(".cert.cbor", certificate_bytes, certificate_size,
                           cert_path, sizeof(cert_path), error, error_size)) {
    return false;
  }
  snprintf(proof_path, sizeof(proof_path), "/tmp/etb-proof-%ld.proof",
           (long)getpid());
  fd = open(proof_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd < 0) {
    unlink(cert_path);
    snprintf(error, error_size, "failed to prepare proof output");
    return false;
  }
  close(fd);
  if (!etb_run_prover_command(prover_path, "prove", cert_path, proof_path, error,
                              error_size) ||
      !etb_read_file_bytes(proof_path, &proof_bytes, &proof_size, error,
                           error_size)) {
    unlink(cert_path);
    unlink(proof_path);
    free(proof_bytes);
    return false;
  }
  unlink(cert_path);
  unlink(proof_path);
  *proof_bytes_out = proof_bytes;
  *proof_size_out = proof_size;
  return true;
}

bool etb_bundle_verify_with_prover(const char *prover_path,
                                   const etb_bundle *bundle, char *error,
                                   size_t error_size) {
  char cert_path[256];
  char proof_path[256];
  if (bundle->proof_size == 0U) {
    snprintf(error, error_size, "bundle for %s has no proof",
             bundle->node_id == NULL ? "unknown-node" : bundle->node_id);
    return false;
  }
  if (!etb_write_temp_file(".cert.cbor", bundle->certificate_bytes,
                           bundle->certificate_size, cert_path,
                           sizeof(cert_path), error, error_size) ||
      !etb_write_temp_file(".proof", bundle->proof_bytes, bundle->proof_size,
                           proof_path, sizeof(proof_path), error, error_size)) {
    unlink(cert_path);
    unlink(proof_path);
    return false;
  }
  if (!etb_run_prover_command(prover_path, "verify", cert_path, proof_path, error,
                              error_size)) {
    unlink(cert_path);
    unlink(proof_path);
    return false;
  }
  unlink(cert_path);
  unlink(proof_path);
  return true;
}

static bool etb_encode_bundle(etb_cbor_buffer *buffer, const etb_bundle *bundle) {
  return etb_cbor_write_map_header(buffer, 4U) &&
         etb_cbor_write_text(buffer, "node") &&
         etb_cbor_write_text(buffer, bundle->node_id) &&
         etb_cbor_write_text(buffer, "query") &&
         etb_cbor_write_text(buffer, bundle->query_text) &&
         etb_cbor_write_text(buffer, "cert") &&
         etb_cbor_write_bytes(buffer, bundle->certificate_bytes,
                              bundle->certificate_size) &&
         etb_cbor_write_text(buffer, "proof") &&
         (bundle->proof_size == 0U
              ? etb_cbor_write_null(buffer)
              : etb_cbor_write_bytes(buffer, bundle->proof_bytes,
                                     bundle->proof_size));
}

static bool etb_encode_routes(etb_cbor_buffer *buffer,
                              const etb_peer_route_map *routes) {
  size_t index;
  size_t publishable = 0U;
  for (index = 0U; index < routes->count; ++index) {
    if (etb_route_is_publishable(routes->items[index].principal,
                                 routes->items[index].endpoint)) {
      publishable += 1U;
    }
  }
  if (!etb_cbor_write_array_header(buffer, publishable)) {
    return false;
  }
  for (index = 0U; index < routes->count; ++index) {
    if (!etb_route_is_publishable(routes->items[index].principal,
                                  routes->items[index].endpoint)) {
      continue;
    }
    if (!etb_cbor_write_map_header(buffer, 2U) ||
        !etb_cbor_write_text(buffer, "principal") ||
        !etb_cbor_write_text(buffer, routes->items[index].principal) ||
        !etb_cbor_write_text(buffer, "endpoint") ||
        !etb_cbor_write_text(buffer, routes->items[index].endpoint)) {
      return false;
    }
  }
  return true;
}

static bool etb_decode_routes(etb_cbor_cursor *cursor,
                              etb_peer_route_map *routes) {
  size_t route_count;
  size_t route_index;
  etb_peer_route_map_init(routes);
  if (!etb_cbor_read_array_header(cursor, &route_count)) {
    return false;
  }
  for (route_index = 0U; route_index < route_count; ++route_index) {
    size_t pair_count;
    size_t pair_index;
    char *principal = NULL;
    char *endpoint = NULL;
    if (!etb_cbor_read_map_header(cursor, &pair_count)) {
      free(principal);
      free(endpoint);
      etb_peer_route_map_free(routes);
      return false;
    }
    for (pair_index = 0U; pair_index < pair_count; ++pair_index) {
      char *key = NULL;
      if (!etb_cbor_read_text(cursor, &key)) {
        free(key);
        free(principal);
        free(endpoint);
        etb_peer_route_map_free(routes);
        return false;
      }
      if (strcmp(key, "principal") == 0) {
        if (!etb_cbor_read_text(cursor, &principal)) {
          free(key);
          free(principal);
          free(endpoint);
          etb_peer_route_map_free(routes);
          return false;
        }
      } else if (strcmp(key, "endpoint") == 0) {
        if (!etb_cbor_read_text(cursor, &endpoint)) {
          free(key);
          free(principal);
          free(endpoint);
          etb_peer_route_map_free(routes);
          return false;
        }
      } else {
        free(key);
        free(principal);
        free(endpoint);
        etb_peer_route_map_free(routes);
        return false;
      }
      free(key);
    }
    if (principal == NULL || endpoint == NULL ||
        !etb_peer_route_map_add(routes, principal, endpoint)) {
      free(principal);
      free(endpoint);
      etb_peer_route_map_free(routes);
      return false;
    }
    free(principal);
    free(endpoint);
  }
  return true;
}

static bool etb_decode_bundle(etb_cbor_cursor *cursor, etb_bundle *bundle) {
  size_t pair_count;
  size_t index;
  memset(bundle, 0, sizeof(*bundle));
  if (!etb_cbor_read_map_header(cursor, &pair_count)) {
    return false;
  }
  for (index = 0U; index < pair_count; ++index) {
    char *key = NULL;
    if (!etb_cbor_read_text(cursor, &key)) {
      free(key);
      return false;
    }
    if (strcmp(key, "node") == 0) {
      if (!etb_cbor_read_text(cursor, &bundle->node_id)) {
        free(key);
        return false;
      }
    } else if (strcmp(key, "query") == 0) {
      if (!etb_cbor_read_text(cursor, &bundle->query_text)) {
        free(key);
        return false;
      }
    } else if (strcmp(key, "cert") == 0) {
      if (!etb_cbor_read_bytes(cursor, &bundle->certificate_bytes,
                               &bundle->certificate_size)) {
        free(key);
        return false;
      }
    } else if (strcmp(key, "proof") == 0) {
      if (cursor->offset < cursor->size && cursor->data[cursor->offset] == 0xf6U) {
        if (!etb_cbor_read_null(cursor)) {
          free(key);
          return false;
        }
      } else if (!etb_cbor_read_bytes(cursor, &bundle->proof_bytes,
                                      &bundle->proof_size)) {
        free(key);
        return false;
      }
    } else {
      free(key);
      return false;
    }
    free(key);
  }
  return bundle->node_id != NULL && bundle->query_text != NULL &&
         bundle->certificate_bytes != NULL;
}

typedef struct etb_request {
  char *op;
  char *query_text;
  char *node_id;
  char *endpoint;
  char *principal;
  etb_peer_route_map routes;
} etb_request;

static void etb_request_init(etb_request *request) {
  memset(request, 0, sizeof(*request));
  etb_peer_route_map_init(&request->routes);
}

static void etb_request_free(etb_request *request) {
  free(request->op);
  free(request->query_text);
  free(request->node_id);
  free(request->endpoint);
  free(request->principal);
  etb_peer_route_map_free(&request->routes);
  memset(request, 0, sizeof(*request));
}

static bool etb_response_encode_ok(const etb_bundle_list *bundles,
                                   const etb_peer_route_map *routes,
                                   const char *endpoint,
                                   unsigned char **payload_out,
                                   size_t *payload_size_out) {
  etb_cbor_buffer buffer;
  size_t index;
  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 5U) ||
      !etb_cbor_write_text(&buffer, "ok") ||
      !etb_cbor_write_bool(&buffer, true) ||
      !etb_cbor_write_text(&buffer, "error") ||
      !etb_cbor_write_text(&buffer, "") ||
      !etb_cbor_write_text(&buffer, "bundles") ||
      !etb_cbor_write_array_header(&buffer, bundles->count)) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  for (index = 0U; index < bundles->count; ++index) {
    if (!etb_encode_bundle(&buffer, &bundles->items[index])) {
      etb_cbor_buffer_free(&buffer);
      return false;
    }
  }
  if (!etb_cbor_write_text(&buffer, "routes")) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  if (!etb_encode_routes(&buffer, routes)) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  if (!etb_cbor_write_text(&buffer, "endpoint")) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  if (!(endpoint == NULL ? etb_cbor_write_null(&buffer)
                         : etb_cbor_write_text(&buffer, endpoint))) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  *payload_out = buffer.data;
  *payload_size_out = buffer.size;
  return true;
}

static bool etb_response_encode_error(const char *message,
                                      unsigned char **payload_out,
                                      size_t *payload_size_out) {
  etb_cbor_buffer buffer;
  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 5U) ||
      !etb_cbor_write_text(&buffer, "ok") ||
      !etb_cbor_write_bool(&buffer, false) ||
      !etb_cbor_write_text(&buffer, "error") ||
      !etb_cbor_write_text(&buffer, message) ||
      !etb_cbor_write_text(&buffer, "bundles") ||
      !etb_cbor_write_array_header(&buffer, 0U) ||
      !etb_cbor_write_text(&buffer, "routes") ||
      !etb_cbor_write_array_header(&buffer, 0U) ||
      !etb_cbor_write_text(&buffer, "endpoint") ||
      !etb_cbor_write_null(&buffer)) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  *payload_out = buffer.data;
  *payload_size_out = buffer.size;
  return true;
}

static bool etb_decode_response(const unsigned char *payload, size_t payload_size,
                                etb_bundle_list *bundles,
                                etb_peer_route_map *routes, char *endpoint,
                                size_t endpoint_size, char *error,
                                size_t error_size) {
  etb_cbor_cursor cursor;
  size_t pair_count;
  size_t index;
  bool ok = false;
  bool saw_ok = false;

  etb_bundle_list_init(bundles);
  etb_peer_route_map_init(routes);
  if (endpoint != NULL && endpoint_size > 0U) {
    endpoint[0] = '\0';
  }
  etb_cbor_cursor_init(&cursor, payload, payload_size);
  if (!etb_cbor_read_map_header(&cursor, &pair_count)) {
    snprintf(error, error_size, "invalid response");
    return false;
  }
  for (index = 0U; index < pair_count; ++index) {
    char *key = NULL;
    if (!etb_cbor_read_text(&cursor, &key)) {
      snprintf(error, error_size, "invalid response field");
      free(key);
      return false;
    }
    if (strcmp(key, "ok") == 0) {
      if (!etb_cbor_read_bool(&cursor, &ok)) {
        free(key);
        snprintf(error, error_size, "invalid response status");
        return false;
      }
      saw_ok = true;
    } else if (strcmp(key, "error") == 0) {
      char *message = NULL;
      if (!etb_cbor_read_text(&cursor, &message)) {
        free(key);
        snprintf(error, error_size, "invalid response error");
        return false;
      }
      if (message[0] != '\0') {
        snprintf(error, error_size, "%s", message);
      }
      free(message);
    } else if (strcmp(key, "bundles") == 0) {
      size_t bundle_count;
      size_t bundle_index;
      if (!etb_cbor_read_array_header(&cursor, &bundle_count)) {
        free(key);
        snprintf(error, error_size, "invalid response bundles");
        return false;
      }
      for (bundle_index = 0U; bundle_index < bundle_count; ++bundle_index) {
        etb_bundle bundle;
        if (!etb_decode_bundle(&cursor, &bundle)) {
          free(key);
          snprintf(error, error_size, "invalid bundle");
          return false;
        }
        if (!etb_bundle_list_add_copy(bundles, bundle.node_id, bundle.query_text,
                                      bundle.certificate_bytes,
                                      bundle.certificate_size,
                                      bundle.proof_bytes, bundle.proof_size)) {
          etb_bundle_free(&bundle);
          free(key);
          snprintf(error, error_size, "out of memory");
          return false;
        }
        etb_bundle_free(&bundle);
      }
    } else if (strcmp(key, "routes") == 0) {
      etb_peer_route_map decoded;
      if (!etb_decode_routes(&cursor, &decoded)) {
        free(key);
        snprintf(error, error_size, "invalid response routes");
        return false;
      }
      if (!etb_peer_route_map_merge(routes, &decoded)) {
        etb_peer_route_map_free(&decoded);
        free(key);
        snprintf(error, error_size, "out of memory");
        return false;
      }
      etb_peer_route_map_free(&decoded);
    } else if (strcmp(key, "endpoint") == 0) {
      if (cursor.offset < cursor.size && cursor.data[cursor.offset] == 0xf6U) {
        if (!etb_cbor_read_null(&cursor)) {
          free(key);
          snprintf(error, error_size, "invalid response endpoint");
          return false;
        }
      } else {
        char *text = NULL;
        if (!etb_cbor_read_text(&cursor, &text)) {
          free(key);
          snprintf(error, error_size, "invalid response endpoint");
          return false;
        }
        if (endpoint != NULL && endpoint_size > 0U) {
          snprintf(endpoint, endpoint_size, "%s", text);
        }
        free(text);
      }
    } else {
      free(key);
      snprintf(error, error_size, "unknown response field");
      return false;
    }
    free(key);
  }
  if (!saw_ok || !ok) {
    if (error[0] == '\0') {
      snprintf(error, error_size, "remote query failed");
    }
    etb_bundle_list_free(bundles);
    etb_peer_route_map_free(routes);
    return false;
  }
  return true;
}

static bool etb_encode_query_request(const etb_node_context *context,
                                     const char *query_text,
                                     unsigned char **payload_out,
                                     size_t *payload_size_out) {
  etb_cbor_buffer buffer;
  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 5U) ||
      !etb_cbor_write_text(&buffer, "op") ||
      !etb_cbor_write_text(&buffer, "query") ||
      !etb_cbor_write_text(&buffer, "query") ||
      !etb_cbor_write_text(&buffer, query_text) ||
      !etb_cbor_write_text(&buffer, "node") ||
      !etb_cbor_write_text(&buffer, context->node_id) ||
      !etb_cbor_write_text(&buffer, "endpoint") ||
      !etb_cbor_write_text(&buffer, context->listen_endpoint) ||
      !etb_cbor_write_text(&buffer, "routes") ||
      !etb_encode_routes(&buffer, &context->local_routes)) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  *payload_out = buffer.data;
  *payload_size_out = buffer.size;
  return true;
}

static bool etb_encode_announce_request(const etb_node_context *context,
                                        unsigned char **payload_out,
                                        size_t *payload_size_out) {
  etb_cbor_buffer buffer;
  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 4U) ||
      !etb_cbor_write_text(&buffer, "op") ||
      !etb_cbor_write_text(&buffer, "announce") ||
      !etb_cbor_write_text(&buffer, "node") ||
      !etb_cbor_write_text(&buffer, context->node_id) ||
      !etb_cbor_write_text(&buffer, "endpoint") ||
      !etb_cbor_write_text(&buffer, context->listen_endpoint) ||
      !etb_cbor_write_text(&buffer, "routes") ||
      !etb_encode_routes(&buffer, &context->local_routes)) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  *payload_out = buffer.data;
  *payload_size_out = buffer.size;
  return true;
}

static bool etb_encode_registry_request(unsigned char **payload_out,
                                        size_t *payload_size_out) {
  etb_cbor_buffer buffer;
  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 1U) ||
      !etb_cbor_write_text(&buffer, "op") ||
      !etb_cbor_write_text(&buffer, "registry")) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  *payload_out = buffer.data;
  *payload_size_out = buffer.size;
  return true;
}

static bool etb_encode_resolve_request(const char *principal,
                                       unsigned char **payload_out,
                                       size_t *payload_size_out) {
  etb_cbor_buffer buffer;
  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 2U) ||
      !etb_cbor_write_text(&buffer, "op") ||
      !etb_cbor_write_text(&buffer, "resolve") ||
      !etb_cbor_write_text(&buffer, "principal") ||
      !etb_cbor_write_text(&buffer, principal)) {
    etb_cbor_buffer_free(&buffer);
    return false;
  }
  *payload_out = buffer.data;
  *payload_size_out = buffer.size;
  return true;
}

static bool etb_decode_request(const unsigned char *payload,
                               size_t payload_size, etb_request *request,
                               char *error, size_t error_size) {
  etb_cbor_cursor cursor;
  size_t pair_count;
  size_t index;
  etb_request_init(request);
  etb_cbor_cursor_init(&cursor, payload, payload_size);
  if (!etb_cbor_read_map_header(&cursor, &pair_count)) {
    snprintf(error, error_size, "invalid request");
    return false;
  }
  for (index = 0U; index < pair_count; ++index) {
    char *key = NULL;
    if (!etb_cbor_read_text(&cursor, &key)) {
      free(key);
      snprintf(error, error_size, "invalid request field");
      return false;
    }
    if (strcmp(key, "op") == 0) {
      if (!etb_cbor_read_text(&cursor, &request->op)) {
        free(key);
        snprintf(error, error_size, "invalid operation");
        return false;
      }
    } else if (strcmp(key, "query") == 0) {
      if (!etb_cbor_read_text(&cursor, &request->query_text)) {
        free(key);
        snprintf(error, error_size, "invalid query text");
        return false;
      }
    } else if (strcmp(key, "node") == 0) {
      if (!etb_cbor_read_text(&cursor, &request->node_id)) {
        free(key);
        snprintf(error, error_size, "invalid node id");
        return false;
      }
    } else if (strcmp(key, "endpoint") == 0) {
      if (!etb_cbor_read_text(&cursor, &request->endpoint)) {
        free(key);
        snprintf(error, error_size, "invalid request endpoint");
        return false;
      }
    } else if (strcmp(key, "routes") == 0) {
      etb_peer_route_map decoded;
      if (!etb_decode_routes(&cursor, &decoded)) {
        free(key);
        snprintf(error, error_size, "invalid request routes");
        return false;
      }
      if (!etb_peer_route_map_merge(&request->routes, &decoded)) {
        etb_peer_route_map_free(&decoded);
        free(key);
        snprintf(error, error_size, "out of memory");
        return false;
      }
      etb_peer_route_map_free(&decoded);
    } else if (strcmp(key, "principal") == 0) {
      if (!etb_cbor_read_text(&cursor, &request->principal)) {
        free(key);
        snprintf(error, error_size, "invalid request principal");
        return false;
      }
    } else {
      free(key);
      snprintf(error, error_size, "unknown request field");
      etb_request_free(request);
      return false;
    }
    free(key);
  }
  if (request->op == NULL) {
    snprintf(error, error_size, "missing operation");
    etb_request_free(request);
    return false;
  }
  return true;
}

static bool etb_registry_snapshot(const etb_node_context *context,
                                  etb_peer_route_map *routes) {
  if (!etb_registry_load(context->registry_path, routes)) {
    return false;
  }
  if (!etb_peer_route_map_merge(routes, &context->local_routes) ||
      !etb_peer_route_map_merge(routes, context->peers)) {
    etb_peer_route_map_free(routes);
    return false;
  }
  return true;
}

static bool etb_registry_ingest(const etb_node_context *context,
                                const etb_request *request) {
  etb_peer_route_map routes;
  if (!etb_registry_snapshot(context, &routes)) {
    return false;
  }
  if (etb_route_is_publishable(request->node_id, request->endpoint)) {
    (void)etb_peer_route_map_add(&routes, request->node_id, request->endpoint);
  }
  if (!etb_peer_route_map_merge(&routes, &request->routes)) {
    etb_peer_route_map_free(&routes);
    return false;
  }
  if (!etb_registry_save(context->registry_path, &routes)) {
    etb_peer_route_map_free(&routes);
    return false;
  }
  etb_peer_route_map_free(&routes);
  return true;
}

static bool etb_send_request(const char *endpoint, const unsigned char *request,
                             size_t request_size, etb_bundle_list *bundles,
                             etb_peer_route_map *routes, char *resolved_endpoint,
                             size_t resolved_endpoint_size, char *error,
                             size_t error_size) {
  int fd = -1;
  unsigned char *response = NULL;
  size_t response_size = 0U;
  if (!etb_connect_endpoint(endpoint, &fd, error, error_size) ||
      !etb_send_message(fd, request, request_size) ||
      !etb_recv_message(fd, &response, &response_size) ||
      !etb_decode_response(response, response_size, bundles, routes,
                           resolved_endpoint, resolved_endpoint_size, error,
                           error_size)) {
    free(response);
    if (fd >= 0) {
      close(fd);
    }
    return false;
  }
  free(response);
  close(fd);
  return true;
}

static bool etb_discovery_sync_with_seed(const etb_node_context *context,
                                         const char *endpoint, char *error,
                                         size_t error_size) {
  unsigned char *request = NULL;
  size_t request_size = 0U;
  etb_bundle_list bundles;
  etb_peer_route_map routes;
  bool ok = false;
  char resolved[8];
  etb_bundle_list_init(&bundles);
  etb_peer_route_map_init(&routes);
  resolved[0] = '\0';
  if (etb_encode_announce_request(context, &request, &request_size) &&
      etb_send_request(endpoint, request, request_size, &bundles, &routes,
                       resolved, sizeof(resolved), error, error_size) &&
      etb_registry_merge_and_save(context->registry_path, &routes)) {
    ok = true;
  }
  free(request);
  etb_bundle_list_free(&bundles);
  etb_peer_route_map_free(&routes);
  return ok;
}

static bool etb_discovery_pull_from_seed(const etb_node_context *context,
                                         const char *endpoint, char *error,
                                         size_t error_size) {
  unsigned char *request = NULL;
  size_t request_size = 0U;
  etb_bundle_list bundles;
  etb_peer_route_map routes;
  bool ok = false;
  char resolved[256];
  etb_bundle_list_init(&bundles);
  etb_peer_route_map_init(&routes);
  resolved[0] = '\0';
  if (etb_encode_registry_request(&request, &request_size) &&
      etb_send_request(endpoint, request, request_size, &bundles, &routes,
                       resolved, sizeof(resolved), error, error_size) &&
      etb_registry_merge_and_save(context->registry_path, &routes)) {
    ok = true;
  }
  free(request);
  etb_bundle_list_free(&bundles);
  etb_peer_route_map_free(&routes);
  return ok;
}

static bool etb_discovery_resolve_with_seed(const etb_node_context *context,
                                            const char *seed_endpoint,
                                            const char *principal,
                                            char *endpoint_out,
                                            size_t endpoint_out_size,
                                            char *error,
                                            size_t error_size) {
  unsigned char *request = NULL;
  size_t request_size = 0U;
  etb_bundle_list bundles;
  etb_peer_route_map routes;
  bool ok = false;
  endpoint_out[0] = '\0';
  etb_bundle_list_init(&bundles);
  etb_peer_route_map_init(&routes);
  if (etb_encode_resolve_request(principal, &request, &request_size) &&
      etb_send_request(seed_endpoint, request, request_size, &bundles, &routes,
                       endpoint_out, endpoint_out_size, error, error_size) &&
      etb_registry_merge_and_save(context->registry_path, &routes)) {
    ok = endpoint_out[0] != '\0';
  }
  free(request);
  etb_bundle_list_free(&bundles);
  etb_peer_route_map_free(&routes);
  return ok;
}

static const char *etb_resolve_known_endpoint(const etb_node_context *context,
                                              const char *principal,
                                              etb_peer_route_map *snapshot) {
  const char *endpoint = etb_peer_route_map_lookup(context->peers, principal);
  etb_peer_route_map_init(snapshot);
  if (endpoint != NULL) {
    return endpoint;
  }
  if (!etb_registry_snapshot(context, snapshot)) {
    return NULL;
  }
  return etb_peer_route_map_lookup(snapshot, principal);
}

static bool etb_resolve_discovery_endpoint(const etb_node_context *context,
                                           const char *principal,
                                           char *endpoint_out,
                                           size_t endpoint_out_size,
                                           char *error,
                                           size_t error_size) {
  etb_peer_route_map snapshot;
  const char *known;
  size_t seed_index;
  endpoint_out[0] = '\0';
  known = etb_resolve_known_endpoint(context, principal, &snapshot);
  if (known != NULL) {
    snprintf(endpoint_out, endpoint_out_size, "%s", known);
    etb_peer_route_map_free(&snapshot);
    return true;
  }
  etb_peer_route_map_free(&snapshot);
  for (seed_index = 0U; seed_index < context->seeds->count; ++seed_index) {
    char resolved[256];
    resolved[0] = '\0';
    if (etb_discovery_resolve_with_seed(context, context->seeds->items[seed_index],
                                        principal, resolved, sizeof(resolved),
                                        error, error_size)) {
      snprintf(endpoint_out, endpoint_out_size, "%s", resolved);
      return true;
    }
  }
  snprintf(error, error_size, "no route for principal '%s'", principal);
  return false;
}

static bool etb_program_has_derivation_for_goal(const etb_program *program,
                                                const etb_atom *goal) {
  size_t index;
  for (index = 0U; index < program->count; ++index) {
    etb_binding_set bindings;
    if (program->items[index].kind != ETB_STMT_CLAUSE) {
      continue;
    }
    etb_binding_set_init(&bindings);
    if (etb_unify_atom(&bindings, &program->items[index].as.clause.head, goal,
                       false)) {
      etb_binding_set_free(&bindings);
      return true;
    }
    etb_binding_set_free(&bindings);
  }
  return false;
}

static bool etb_import_remote_bundle(const etb_bundle *bundle,
                                     const etb_node_context *context,
                                     etb_engine *engine, char *error,
                                     size_t error_size) {
  etb_certificate certificate;
  etb_certificate_init(&certificate);
  if (context->prover_path != NULL &&
      !etb_bundle_verify_with_prover(context->prover_path, bundle, error,
                                     error_size)) {
    etb_certificate_free(&certificate);
    return false;
  }
  if (!etb_certificate_read_bytes(bundle->certificate_bytes,
                                  bundle->certificate_size, &certificate, error,
                                  error_size) ||
      !etb_engine_import_answers(engine, certificate.answers,
                                 certificate.answer_count,
                                 certificate.root_digest, error, error_size)) {
    etb_certificate_free(&certificate);
    return false;
  }
  etb_certificate_free(&certificate);
  return true;
}

static bool etb_resolve_goal_dependencies(const etb_node_context *context,
                                          const etb_program *program,
                                          etb_engine *engine,
                                          const etb_atom *goal,
                                          etb_bundle_list *bundles,
                                          etb_string_list *fetched_queries,
                                          etb_string_list *visited_goals,
                                          char *error, size_t error_size);

static bool etb_resolve_clause_dependencies(const etb_node_context *context,
                                            const etb_program *program,
                                            etb_engine *engine,
                                            const etb_clause *clause,
                                            const etb_atom *goal,
                                            etb_bundle_list *bundles,
                                            etb_string_list *fetched_queries,
                                            etb_string_list *visited_goals,
                                            char *error, size_t error_size) {
  etb_binding_set bindings;
  size_t literal_index;
  etb_binding_set_init(&bindings);
  if (!etb_unify_atom(&bindings, &clause->head, goal, false)) {
    etb_binding_set_free(&bindings);
    return true;
  }
  for (literal_index = 0U; literal_index < clause->body.count; ++literal_index) {
    const etb_literal *literal = &clause->body.items[literal_index];
    etb_atom instantiated;
    etb_fact_list answers;
    char *remote_query = NULL;
    char resolved_endpoint[256];
    const char *endpoint;
    if (literal->negated || etb_literal_is_capability(engine, literal)) {
      continue;
    }
    if (!etb_instantiate_atom(&literal->atom, &bindings, &instantiated)) {
      etb_binding_set_free(&bindings);
      snprintf(error, error_size, "failed to instantiate dependency");
      return false;
    }
    resolved_endpoint[0] = '\0';
    endpoint = NULL;
    if (instantiated.kind == ETB_ATOM_SAYS &&
        etb_resolve_discovery_endpoint(context, instantiated.principal,
                                       resolved_endpoint,
                                       sizeof(resolved_endpoint), error,
                                       error_size)) {
      endpoint = resolved_endpoint;
    } else if (instantiated.kind == ETB_ATOM_SAYS) {
      etb_atom_free(&instantiated);
      etb_binding_set_free(&bindings);
      return false;
    }
    if (endpoint != NULL) {
      etb_bundle_list remote_bundles;
      etb_bundle_list_init(&remote_bundles);
      if (!etb_atom_canonical_text(&instantiated, &remote_query)) {
        etb_atom_free(&instantiated);
        etb_binding_set_free(&bindings);
        snprintf(error, error_size, "failed to canonicalize remote query");
        return false;
      }
      if (!etb_string_list_contains(fetched_queries, remote_query)) {
        if (!etb_remote_query(endpoint, remote_query, &remote_bundles, error,
                              error_size)) {
          free(remote_query);
          etb_atom_free(&instantiated);
          etb_bundle_list_free(&remote_bundles);
          etb_binding_set_free(&bindings);
          return false;
        }
        for (size_t bundle_index = 0U; bundle_index < remote_bundles.count;
             ++bundle_index) {
          if (!etb_import_remote_bundle(&remote_bundles.items[bundle_index], context,
                                        engine, error, error_size)) {
            free(remote_query);
            etb_atom_free(&instantiated);
            etb_bundle_list_free(&remote_bundles);
            etb_binding_set_free(&bindings);
            return false;
          }
        }
        if (!etb_bundle_list_append_unique(bundles, &remote_bundles) ||
            !etb_string_list_add(fetched_queries, remote_query) ||
            !etb_engine_run_fixpoint(engine, error, error_size)) {
          free(remote_query);
          etb_atom_free(&instantiated);
          etb_bundle_list_free(&remote_bundles);
          etb_binding_set_free(&bindings);
          return false;
        }
      }
      etb_bundle_list_free(&remote_bundles);
      free(remote_query);
      etb_atom_free(&instantiated);
      continue;
    }
    etb_fact_list_init(&answers);
    if (!etb_engine_query(engine, &instantiated, &answers, error, error_size)) {
      etb_fact_list_free(&answers);
      etb_atom_free(&instantiated);
      etb_binding_set_free(&bindings);
      return false;
    }
    if (answers.count == 0U &&
        etb_program_has_derivation_for_goal(program, &instantiated) &&
        !etb_resolve_goal_dependencies(context, program, engine, &instantiated,
                                       bundles, fetched_queries, visited_goals,
                                       error, error_size)) {
      etb_fact_list_free(&answers);
      etb_atom_free(&instantiated);
      etb_binding_set_free(&bindings);
      return false;
    }
    etb_fact_list_free(&answers);
    etb_atom_free(&instantiated);
  }
  etb_binding_set_free(&bindings);
  return true;
}

static bool etb_resolve_goal_dependencies(const etb_node_context *context,
                                          const etb_program *program,
                                          etb_engine *engine,
                                          const etb_atom *goal,
                                          etb_bundle_list *bundles,
                                          etb_string_list *fetched_queries,
                                          etb_string_list *visited_goals,
                                          char *error, size_t error_size) {
  char *goal_text = NULL;
  size_t index;
  etb_fact_list answers;

  if (!etb_atom_canonical_text(goal, &goal_text)) {
    snprintf(error, error_size, "failed to canonicalize goal");
    return false;
  }
  if (etb_string_list_contains(visited_goals, goal_text)) {
    free(goal_text);
    return true;
  }
  if (!etb_string_list_add(visited_goals, goal_text)) {
    free(goal_text);
    snprintf(error, error_size, "out of memory");
    return false;
  }
  free(goal_text);

  etb_fact_list_init(&answers);
  if (!etb_engine_query(engine, goal, &answers, error, error_size)) {
    etb_fact_list_free(&answers);
    return false;
  }
  if (answers.count > 0U) {
    etb_fact_list_free(&answers);
    return true;
  }
  etb_fact_list_free(&answers);

  for (index = 0U; index < program->count; ++index) {
    if (program->items[index].kind != ETB_STMT_CLAUSE) {
      continue;
    }
    if (!etb_resolve_clause_dependencies(context, program, engine,
                                         &program->items[index].as.clause, goal,
                                         bundles, fetched_queries,
                                         visited_goals, error, error_size)) {
      return false;
    }
  }
  return true;
}

static bool etb_execute_query(const etb_node_context *context,
                              const char *query_text,
                              etb_bundle_list *bundles, char *error,
                              size_t error_size) {
  etb_program program;
  etb_engine engine;
  etb_atom query;
  etb_fact_list answers;
  etb_certificate certificate;
  unsigned char *proof_bytes = NULL;
  size_t proof_size = 0U;
  etb_string_list fetched_queries;
  etb_string_list visited_goals;
  size_t index;

  etb_program_init(&program);
  etb_engine_init(&engine);
  etb_atom_init(&query);
  etb_fact_list_init(&answers);
  etb_certificate_init(&certificate);
  etb_bundle_list_init(bundles);
  etb_string_list_init(&fetched_queries);
  etb_string_list_init(&visited_goals);

  if (!etb_parse_file(context->program_path, &program, error, error_size) ||
      !etb_engine_load_program(&engine, &program, error, error_size) ||
      !etb_engine_run_fixpoint(&engine, error, error_size) ||
      !etb_parse_atom_text(query_text, &query, error, error_size) ||
      !etb_resolve_goal_dependencies(context, &program, &engine, &query, bundles,
                                     &fetched_queries, &visited_goals, error,
                                     error_size) ||
      !etb_engine_run_fixpoint(&engine, error, error_size) ||
      !etb_engine_query(&engine, &query, &answers, error, error_size)) {
    etb_string_list_free(&visited_goals);
    etb_string_list_free(&fetched_queries);
    etb_bundle_list_free(bundles);
    etb_certificate_free(&certificate);
    etb_fact_list_free(&answers);
    etb_atom_free(&query);
    etb_engine_free(&engine);
    etb_program_free(&program);
    return false;
  }
  if (answers.count == 0U) {
    snprintf(error, error_size, "query produced no answers");
    etb_string_list_free(&visited_goals);
    etb_string_list_free(&fetched_queries);
    etb_bundle_list_free(bundles);
    etb_certificate_free(&certificate);
    etb_fact_list_free(&answers);
    etb_atom_free(&query);
    etb_engine_free(&engine);
    etb_program_free(&program);
    return false;
  }
  {
    etb_atom *atoms = (etb_atom *)calloc(answers.count, sizeof(etb_atom));
    if (atoms == NULL) {
      snprintf(error, error_size, "out of memory");
      etb_string_list_free(&visited_goals);
      etb_string_list_free(&fetched_queries);
      etb_bundle_list_free(bundles);
      etb_certificate_free(&certificate);
      etb_fact_list_free(&answers);
      etb_atom_free(&query);
      etb_engine_free(&engine);
      etb_program_free(&program);
      return false;
    }
    for (index = 0U; index < answers.count; ++index) {
      atoms[index] = etb_atom_clone(&answers.items[index].atom);
    }
    if (!etb_certificate_build(&certificate, &query, atoms, answers.count,
                               &engine.trace)) {
      free(atoms);
      snprintf(error, error_size, "failed to build certificate");
      etb_string_list_free(&visited_goals);
      etb_string_list_free(&fetched_queries);
      etb_bundle_list_free(bundles);
      etb_certificate_free(&certificate);
      etb_fact_list_free(&answers);
      etb_atom_free(&query);
      etb_engine_free(&engine);
      etb_program_free(&program);
      return false;
    }
    for (index = 0U; index < answers.count; ++index) {
      etb_atom_free(&atoms[index]);
    }
    free(atoms);
  }
  if (context->prover_path != NULL &&
      !etb_prove_certificate(context->prover_path, certificate.cbor,
                             certificate.cbor_size, &proof_bytes, &proof_size,
                             error, error_size)) {
    free(proof_bytes);
    etb_string_list_free(&visited_goals);
    etb_string_list_free(&fetched_queries);
    etb_bundle_list_free(bundles);
    etb_certificate_free(&certificate);
    etb_fact_list_free(&answers);
    etb_atom_free(&query);
    etb_engine_free(&engine);
    etb_program_free(&program);
    return false;
  }
  if (!etb_bundle_list_add_copy(bundles, context->node_id, query_text,
                                certificate.cbor, certificate.cbor_size,
                                proof_bytes, proof_size)) {
    snprintf(error, error_size, "failed to store bundle");
    free(proof_bytes);
    etb_string_list_free(&visited_goals);
    etb_string_list_free(&fetched_queries);
    etb_bundle_list_free(bundles);
    etb_certificate_free(&certificate);
    etb_fact_list_free(&answers);
    etb_atom_free(&query);
    etb_engine_free(&engine);
    etb_program_free(&program);
    return false;
  }
  free(proof_bytes);
  etb_string_list_free(&visited_goals);
  etb_string_list_free(&fetched_queries);
  etb_certificate_free(&certificate);
  etb_fact_list_free(&answers);
  etb_atom_free(&query);
  etb_engine_free(&engine);
  etb_program_free(&program);
  return true;
}

bool etb_remote_query(const char *endpoint, const char *query_text,
                      etb_bundle_list *bundles, char *error,
                      size_t error_size) {
  etb_node_context context;
  unsigned char *request = NULL;
  size_t request_size = 0U;
  etb_peer_route_map routes;
  char resolved[8];
  memset(&context, 0, sizeof(context));
  etb_peer_route_map_init(&context.local_routes);
  context.node_id = "etbctl";
  context.listen_endpoint = "";
  if (!etb_encode_query_request(&context, query_text, &request, &request_size) ||
      !etb_send_request(endpoint, request, request_size, bundles, &routes,
                        resolved, sizeof(resolved), error, error_size)) {
    free(request);
    etb_peer_route_map_free(&routes);
    etb_peer_route_map_free(&context.local_routes);
    return false;
  }
  free(request);
  etb_peer_route_map_free(&routes);
  etb_peer_route_map_free(&context.local_routes);
  return true;
}

static bool etb_handle_client(int client_fd, const etb_node_context *context) {
  unsigned char *request = NULL;
  size_t request_size = 0U;
  unsigned char *response = NULL;
  size_t response_size = 0U;
  char error[256];
  etb_bundle_list bundles;
  etb_peer_route_map routes;
  etb_request decoded;
  char resolved_endpoint[256];
  bool ok = false;

  memset(error, 0, sizeof(error));
  etb_bundle_list_init(&bundles);
  etb_peer_route_map_init(&routes);
  etb_request_init(&decoded);
  resolved_endpoint[0] = '\0';
  if (!etb_recv_message(client_fd, &request, &request_size) ||
      !etb_decode_request(request, request_size, &decoded, error,
                          sizeof(error))) {
    (void)etb_response_encode_error(error[0] == '\0' ? "invalid request" : error,
                                    &response, &response_size);
    goto done;
  }
  if ((decoded.routes.count > 0U ||
       (decoded.node_id != NULL && decoded.endpoint != NULL)) &&
      !etb_registry_ingest(context, &decoded)) {
    (void)etb_response_encode_error("failed to update registry", &response,
                                    &response_size);
    goto done;
  }
  if (strcmp(decoded.op, "query") == 0) {
    if (decoded.query_text == NULL ||
        !etb_execute_query(context, decoded.query_text, &bundles, error,
                           sizeof(error))) {
      (void)etb_response_encode_error(error, &response, &response_size);
      goto done;
    }
    if (!etb_registry_snapshot(context, &routes)) {
      (void)etb_response_encode_error("failed to read registry", &response,
                                      &response_size);
      goto done;
    }
  } else if (strcmp(decoded.op, "announce") == 0 ||
             strcmp(decoded.op, "registry") == 0) {
    if (!etb_registry_snapshot(context, &routes)) {
      (void)etb_response_encode_error("failed to read registry", &response,
                                      &response_size);
      goto done;
    }
  } else if (strcmp(decoded.op, "resolve") == 0) {
    if (!etb_registry_snapshot(context, &routes)) {
      (void)etb_response_encode_error("failed to read registry", &response,
                                      &response_size);
      goto done;
    }
    if (decoded.principal != NULL) {
      const char *match = etb_peer_route_map_lookup(&routes, decoded.principal);
      if (match != NULL) {
        snprintf(resolved_endpoint, sizeof(resolved_endpoint), "%s", match);
      }
    }
  } else {
    (void)etb_response_encode_error("unsupported request", &response,
                                    &response_size);
    goto done;
  }
  if (!etb_response_encode_ok(&bundles, &routes,
                              resolved_endpoint[0] == '\0'
                                  ? NULL
                                  : resolved_endpoint,
                              &response, &response_size)) {
    (void)etb_response_encode_error("failed to encode response", &response,
                                    &response_size);
    goto done;
  }
  ok = true;

done:
  if (response != NULL) {
    (void)etb_send_message(client_fd, response, response_size);
  }
  free(response);
  free(request);
  etb_request_free(&decoded);
  etb_bundle_list_free(&bundles);
  etb_peer_route_map_free(&routes);
  return ok;
}

static void etb_discovery_announce_loop(const etb_node_context *context) {
  for (;;) {
    size_t index;
    for (index = 0U; index < context->seeds->count; ++index) {
      char error[256];
      memset(error, 0, sizeof(error));
      (void)etb_discovery_sync_with_seed(context, context->seeds->items[index],
                                         error, sizeof(error));
      (void)etb_discovery_pull_from_seed(context, context->seeds->items[index],
                                         error, sizeof(error));
    }
    sleep(1);
  }
}

bool etb_node_serve(const char *node_id, const char *program_path,
                    const char *listen_endpoint,
                    const etb_peer_route_map *peers,
                    const etb_endpoint_list *seeds,
                    const char *prover_path, char *error,
                    size_t error_size) {
  int listen_fd;
  etb_node_context context;
  etb_peer_route_map initial_routes;
  pid_t announcer_pid;
  char safe_name[128];
  memset(&context, 0, sizeof(context));
  context.node_id = node_id;
  context.program_path = program_path;
  context.listen_endpoint = listen_endpoint;
  context.peers = peers;
  context.seeds = seeds;
  context.prover_path = prover_path;
  etb_peer_route_map_init(&context.local_routes);
  etb_sanitize_name(node_id, safe_name, sizeof(safe_name));
  snprintf(context.registry_path, sizeof(context.registry_path),
           "/tmp/etb-registry-%s.txt", safe_name);
  if (!etb_collect_local_routes(program_path, listen_endpoint, node_id,
                                &context.local_routes, error, error_size)) {
    etb_peer_route_map_free(&context.local_routes);
    return false;
  }
  etb_peer_route_map_init(&initial_routes);
  if (!etb_peer_route_map_merge(&initial_routes, &context.local_routes) ||
      !etb_peer_route_map_merge(&initial_routes, peers) ||
      !etb_registry_save(context.registry_path, &initial_routes)) {
    etb_peer_route_map_free(&initial_routes);
    etb_peer_route_map_free(&context.local_routes);
    snprintf(error, error_size, "failed to initialize registry");
    return false;
  }
  etb_peer_route_map_free(&initial_routes);

  if (!etb_listen_endpoint(listen_endpoint, &listen_fd, error, error_size)) {
    etb_peer_route_map_free(&context.local_routes);
    return false;
  }
  (void)signal(SIGCHLD, SIG_IGN);
  announcer_pid = -1;
  if (seeds != NULL && seeds->count > 0U) {
    announcer_pid = fork();
    if (announcer_pid == 0) {
      close(listen_fd);
      etb_discovery_announce_loop(&context);
      _exit(0);
    }
  }
  printf("etbd[%s] listening on %s\n", node_id, listen_endpoint);
  fflush(stdout);
  for (;;) {
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      snprintf(error, error_size, "accept failed");
      close(listen_fd);
      return false;
    }
    {
      pid_t child = fork();
      if (child == 0) {
        close(listen_fd);
        (void)etb_handle_client(client_fd, &context);
        close(client_fd);
        _exit(0);
      }
      close(client_fd);
    }
  }
}
