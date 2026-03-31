#include "etb/membership.h"

#include <stdlib.h>
#include <string.h>

void etb_membership_init(etb_membership *membership, uint64_t lease_ms) {
  memset(membership, 0, sizeof(*membership));
  membership->lease_ms = lease_ms;
}

void etb_membership_free(etb_membership *membership) {
  size_t index;
  if (membership == NULL) {
    return;
  }
  for (index = 0U; index < membership->count; ++index) {
    free(membership->items[index].node_id);
    free(membership->items[index].endpoint);
  }
  free(membership->items);
  memset(membership, 0, sizeof(*membership));
}

bool etb_membership_upsert(etb_membership *membership,
                           const etb_peer_info *peer) {
  size_t index;
  for (index = 0U; index < membership->count; ++index) {
    if (strcmp(membership->items[index].node_id, peer->node_id) == 0) {
      if (membership->items[index].incarnation <= peer->incarnation) {
        membership->items[index].incarnation = peer->incarnation;
        membership->items[index].last_heartbeat_ms = peer->last_heartbeat_ms;
        membership->items[index].status = peer->status;
      }
      return true;
    }
  }
  if (membership->count == membership->capacity) {
    etb_peer_info *grown;
    membership->capacity = membership->capacity == 0U ? 8U : membership->capacity * 2U;
    grown = (etb_peer_info *)realloc(membership->items,
                                     sizeof(*grown) * membership->capacity);
    if (grown == NULL) {
      return false;
    }
    membership->items = grown;
  }
  membership->items[membership->count].node_id = strdup(peer->node_id);
  membership->items[membership->count].endpoint = strdup(peer->endpoint);
  membership->items[membership->count].incarnation = peer->incarnation;
  membership->items[membership->count].last_heartbeat_ms = peer->last_heartbeat_ms;
  membership->items[membership->count].status = peer->status;
  membership->count += 1U;
  return true;
}

void etb_membership_expire(etb_membership *membership, uint64_t now_ms) {
  size_t index;
  for (index = 0U; index < membership->count; ++index) {
    if (membership->items[index].status != ETB_PEER_DISABLED &&
        membership->items[index].status != ETB_PEER_DRAINING &&
        now_ms > membership->items[index].last_heartbeat_ms + membership->lease_ms) {
      membership->items[index].status = ETB_PEER_DEAD;
    }
  }
}
