#ifndef ETB_MEMBERSHIP_H
#define ETB_MEMBERSHIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum etb_peer_status {
  ETB_PEER_ALIVE = 0,
  ETB_PEER_SUSPECT = 1,
  ETB_PEER_DEAD = 2,
  ETB_PEER_DRAINING = 3,
  ETB_PEER_DISABLED = 4
} etb_peer_status;

typedef struct etb_peer_info {
  char *node_id;
  char *endpoint;
  uint64_t incarnation;
  uint64_t last_heartbeat_ms;
  etb_peer_status status;
} etb_peer_info;

typedef struct etb_membership {
  etb_peer_info *items;
  size_t count;
  size_t capacity;
  uint64_t lease_ms;
} etb_membership;

void etb_membership_init(etb_membership *membership, uint64_t lease_ms);
void etb_membership_free(etb_membership *membership);
bool etb_membership_upsert(etb_membership *membership,
                           const etb_peer_info *peer);
void etb_membership_expire(etb_membership *membership, uint64_t now_ms);
const etb_peer_info *etb_membership_find(const etb_membership *membership,
                                         const char *node_id);

#endif
