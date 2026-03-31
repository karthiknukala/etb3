#ifndef ETB_CERT_H
#define ETB_CERT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etb/trace.h"

typedef struct etb_certificate {
  etb_atom query;
  etb_atom *answers;
  size_t answer_count;
  etb_trace trace;
  char root_digest[65];
  unsigned char *cbor;
  size_t cbor_size;
} etb_certificate;

void etb_certificate_init(etb_certificate *certificate);
void etb_certificate_free(etb_certificate *certificate);
bool etb_certificate_build(etb_certificate *certificate, const etb_atom *query,
                           const etb_atom *answers, size_t answer_count,
                           const etb_trace *trace);
bool etb_certificate_write_file(const etb_certificate *certificate,
                                const char *path, char *error,
                                size_t error_size);
bool etb_certificate_read_file(const char *path, etb_certificate *certificate,
                               char *error, size_t error_size);

#endif
