#include "etb/cert.h"

#include <stdlib.h>
#include <string.h>

#include "../core/canon.h"
#include "../core/cbor.h"
#include "../core/sha256.h"

static bool etb_cbor_write_atom(etb_cbor_buffer *buffer, const etb_atom *atom) {
  char *text = NULL;
  bool ok;
  ok = etb_atom_canonical_text(atom, &text) && etb_cbor_write_text(buffer, text);
  free(text);
  return ok;
}

void etb_certificate_init(etb_certificate *certificate) {
  memset(certificate, 0, sizeof(*certificate));
  etb_trace_init(&certificate->trace);
  etb_atom_init(&certificate->query);
}

void etb_certificate_free(etb_certificate *certificate) {
  size_t index;
  if (certificate == NULL) {
    return;
  }
  etb_atom_free(&certificate->query);
  for (index = 0U; index < certificate->answer_count; ++index) {
    etb_atom_free(&certificate->answers[index]);
  }
  free(certificate->answers);
  free(certificate->cbor);
  etb_trace_free(&certificate->trace);
  memset(certificate, 0, sizeof(*certificate));
}

bool etb_certificate_build(etb_certificate *certificate, const etb_atom *query,
                           const etb_atom *answers, size_t answer_count,
                           const etb_trace *trace) {
  etb_cbor_buffer buffer;
  size_t index;

  etb_certificate_init(certificate);
  certificate->query = etb_atom_clone(query);
  certificate->answers = (etb_atom *)calloc(answer_count, sizeof(etb_atom));
  if (answer_count > 0U && certificate->answers == NULL) {
    return false;
  }
  for (index = 0U; index < answer_count; ++index) {
    certificate->answers[index] = etb_atom_clone(&answers[index]);
  }
  certificate->answer_count = answer_count;
  for (index = 0U; index < trace->count; ++index) {
    if (etb_trace_append(&certificate->trace, &trace->items[index]) == (size_t)-1) {
      etb_certificate_free(certificate);
      return false;
    }
  }

  etb_cbor_buffer_init(&buffer);
  if (!etb_cbor_write_map_header(&buffer, 4U) ||
      !etb_cbor_write_text(&buffer, "query") ||
      !etb_cbor_write_atom(&buffer, query) ||
      !etb_cbor_write_text(&buffer, "answers") ||
      !etb_cbor_write_array_header(&buffer, answer_count) ||
      !etb_cbor_write_text(&buffer, "trace") ||
      !etb_cbor_write_array_header(&buffer, certificate->trace.count) ||
      !etb_cbor_write_text(&buffer, "root")) {
    etb_cbor_buffer_free(&buffer);
    etb_certificate_free(certificate);
    return false;
  }
  for (index = 0U; index < answer_count; ++index) {
    if (!etb_cbor_write_atom(&buffer, &answers[index])) {
      etb_cbor_buffer_free(&buffer);
      etb_certificate_free(certificate);
      return false;
    }
  }
  for (index = 0U; index < certificate->trace.count; ++index) {
    if (!etb_cbor_write_text(&buffer, certificate->trace.items[index].digest_hex)) {
      etb_cbor_buffer_free(&buffer);
      etb_certificate_free(certificate);
      return false;
    }
  }
  etb_sha256_hex(buffer.data, buffer.size, certificate->root_digest);
  if (!etb_cbor_write_text(&buffer, certificate->root_digest)) {
    etb_cbor_buffer_free(&buffer);
    etb_certificate_free(certificate);
    return false;
  }
  certificate->cbor = buffer.data;
  certificate->cbor_size = buffer.size;
  return true;
}
