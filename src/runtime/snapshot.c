#include "etb/registry.h"
#include "etb/signer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool etb_snapshot_bytes(const etb_registry_snapshot *snapshot,
                               unsigned char **data, size_t *size) {
  char *buffer = NULL;
  size_t capacity = 256U;
  size_t used = 0U;
  size_t index;
  buffer = (char *)malloc(capacity);
  if (buffer == NULL) {
    return false;
  }
  used += (size_t)snprintf(buffer + used, capacity - used, "version:%llu\n",
                           (unsigned long long)snapshot->version);
  for (index = 0U; index < snapshot->key_count; ++index) {
    size_t needed = strlen(snapshot->keys[index].principal) +
                    strlen(snapshot->keys[index].public_key_pem) + 64U;
    if (used + needed + 1U > capacity) {
      char *grown;
      capacity = capacity * 2U + needed;
      grown = (char *)realloc(buffer, capacity);
      if (grown == NULL) {
        free(buffer);
        return false;
      }
      buffer = grown;
    }
    used += (size_t)snprintf(buffer + used, capacity - used, "%s|%u|%s|%d\n",
                             snapshot->keys[index].principal,
                             snapshot->keys[index].version,
                             snapshot->keys[index].public_key_pem,
                             snapshot->keys[index].revoked ? 1 : 0);
  }
  *data = (unsigned char *)buffer;
  *size = used;
  return true;
}

bool etb_registry_snapshot_sign(etb_registry_snapshot *snapshot,
                                const char *private_key_path,
                                char *error,
                                size_t error_size) {
  etb_signer signer;
  unsigned char *data = NULL;
  size_t size = 0U;
  char *public_key = NULL;
  if (!etb_snapshot_bytes(snapshot, &data, &size) ||
      !etb_signer_open_soft(&signer, private_key_path, error, error_size) ||
      !etb_signer_sign(&signer, data, size, &snapshot->signature,
                       &snapshot->signature_size, &public_key, error, error_size)) {
    free(data);
    return false;
  }
  etb_signer_close(&signer);
  free(data);
  free(public_key);
  return true;
}

bool etb_registry_snapshot_verify(const etb_registry_snapshot *snapshot,
                                  const char *public_key_pem, char *error,
                                  size_t error_size) {
  unsigned char *data = NULL;
  size_t size = 0U;
  bool ok;
  if (!etb_snapshot_bytes(snapshot, &data, &size)) {
    snprintf(error, error_size, "failed to canonicalize snapshot");
    return false;
  }
  ok = etb_signer_verify(data, size, snapshot->signature, snapshot->signature_size,
                         public_key_pem, error, error_size);
  free(data);
  return ok;
}
