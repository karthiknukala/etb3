#include "etb/membership.h"

#include <string.h>

const etb_peer_info *etb_membership_find(const etb_membership *membership,
                                         const char *node_id) {
  size_t index;
  for (index = 0U; index < membership->count; ++index) {
    if (strcmp(membership->items[index].node_id, node_id) == 0) {
      return &membership->items[index];
    }
  }
  return NULL;
}
