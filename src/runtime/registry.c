#include "etb/registry.h"

#include <stdlib.h>
#include <string.h>

void etb_registry_snapshot_init(etb_registry_snapshot *snapshot) {
  memset(snapshot, 0, sizeof(*snapshot));
}

void etb_registry_snapshot_free(etb_registry_snapshot *snapshot) {
  size_t index;
  if (snapshot == NULL) {
    return;
  }
  for (index = 0U; index < snapshot->key_count; ++index) {
    free(snapshot->keys[index].principal);
    free(snapshot->keys[index].public_key_pem);
  }
  free(snapshot->keys);
  free(snapshot->signature);
  memset(snapshot, 0, sizeof(*snapshot));
}
