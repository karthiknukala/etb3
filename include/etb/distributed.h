#ifndef ETB_DISTRIBUTED_H
#define ETB_DISTRIBUTED_H

#include <stdbool.h>
#include <stddef.h>

typedef struct etb_peer_route {
  char *principal;
  char *endpoint;
} etb_peer_route;

typedef struct etb_peer_route_map {
  etb_peer_route *items;
  size_t count;
  size_t capacity;
} etb_peer_route_map;

typedef struct etb_bundle {
  char *node_id;
  char *query_text;
  unsigned char *certificate_bytes;
  size_t certificate_size;
  unsigned char *proof_bytes;
  size_t proof_size;
} etb_bundle;

typedef struct etb_bundle_list {
  etb_bundle *items;
  size_t count;
  size_t capacity;
} etb_bundle_list;

void etb_peer_route_map_init(etb_peer_route_map *map);
void etb_peer_route_map_free(etb_peer_route_map *map);
bool etb_peer_route_map_add(etb_peer_route_map *map, const char *principal,
                            const char *endpoint);
const char *etb_peer_route_map_lookup(const etb_peer_route_map *map,
                                      const char *principal);

void etb_bundle_list_init(etb_bundle_list *list);
void etb_bundle_list_free(etb_bundle_list *list);
bool etb_bundle_list_add_copy(etb_bundle_list *list, const char *node_id,
                              const char *query_text,
                              const unsigned char *certificate_bytes,
                              size_t certificate_size,
                              const unsigned char *proof_bytes,
                              size_t proof_size);
bool etb_bundle_list_append_unique(etb_bundle_list *dst,
                                   const etb_bundle_list *src);

bool etb_node_serve(const char *node_id, const char *program_path,
                    const char *listen_endpoint,
                    const etb_peer_route_map *peers,
                    const char *prover_path, char *error,
                    size_t error_size);

bool etb_remote_query(const char *endpoint, const char *query_text,
                      etb_bundle_list *bundles, char *error,
                      size_t error_size);

bool etb_bundle_verify_with_prover(const char *prover_path,
                                   const etb_bundle *bundle, char *error,
                                   size_t error_size);

#endif
