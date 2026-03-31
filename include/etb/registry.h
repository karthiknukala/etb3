#ifndef ETB_REGISTRY_H
#define ETB_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct etb_principal_key {
  char *principal;
  uint32_t version;
  char *public_key_pem;
  bool revoked;
} etb_principal_key;

typedef struct etb_registry_snapshot {
  uint64_t version;
  etb_principal_key *keys;
  size_t key_count;
  unsigned char *signature;
  size_t signature_size;
} etb_registry_snapshot;

void etb_registry_snapshot_init(etb_registry_snapshot *snapshot);
void etb_registry_snapshot_free(etb_registry_snapshot *snapshot);
bool etb_registry_snapshot_sign(etb_registry_snapshot *snapshot,
                                const char *private_key_path,
                                char *error,
                                size_t error_size);
bool etb_registry_snapshot_verify(const etb_registry_snapshot *snapshot,
                                  const char *public_key_pem, char *error,
                                  size_t error_size);

#endif
