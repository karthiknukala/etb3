#include "etb/signer.h"

bool etb_signer_open_secure_enclave_alias(etb_signer *signer, const char *label,
                                          char *error, size_t error_size) {
  return etb_signer_open_macos(signer, label, error, error_size);
}
