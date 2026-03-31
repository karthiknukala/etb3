#ifndef ETB_SIGNER_H
#define ETB_SIGNER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum etb_signer_kind {
  ETB_SIGNER_SOFT = 0,
  ETB_SIGNER_MACOS_SECURE_ENCLAVE = 1
} etb_signer_kind;

typedef struct etb_signer {
  etb_signer_kind kind;
  void *impl;
} etb_signer;

bool etb_signer_open_soft(etb_signer *signer, const char *key_path,
                          char *error, size_t error_size);
bool etb_signer_open_macos(etb_signer *signer, const char *label, char *error,
                           size_t error_size);
void etb_signer_close(etb_signer *signer);
bool etb_signer_sign(etb_signer *signer, const unsigned char *data,
                     size_t data_size, unsigned char **signature,
                     size_t *signature_size, char **public_key_pem,
                     char *error, size_t error_size);
bool etb_signer_verify(const unsigned char *data, size_t data_size,
                       const unsigned char *signature, size_t signature_size,
                       const char *public_key_pem, char *error,
                       size_t error_size);

#endif
