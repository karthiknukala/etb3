#include "etb/cert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/canon.h"
#include "../core/cbor.h"
#include "../core/sha256.h"
#include "etb/parser.h"

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
      !etb_cbor_write_array_header(&buffer, answer_count)) {
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
  if (!etb_cbor_write_text(&buffer, "trace") ||
      !etb_cbor_write_array_header(&buffer, certificate->trace.count)) {
    etb_cbor_buffer_free(&buffer);
    etb_certificate_free(certificate);
    return false;
  }
  for (index = 0U; index < certificate->trace.count; ++index) {
    if (!etb_cbor_write_text(&buffer, certificate->trace.items[index].digest_hex)) {
      etb_cbor_buffer_free(&buffer);
      etb_certificate_free(certificate);
      return false;
    }
  }
  etb_sha256_hex(buffer.data, buffer.size, certificate->root_digest);
  if (!etb_cbor_write_text(&buffer, "root") ||
      !etb_cbor_write_text(&buffer, certificate->root_digest)) {
    etb_cbor_buffer_free(&buffer);
    etb_certificate_free(certificate);
    return false;
  }
  certificate->cbor = buffer.data;
  certificate->cbor_size = buffer.size;
  return true;
}

bool etb_certificate_write_file(const etb_certificate *certificate,
                                const char *path, char *error,
                                size_t error_size) {
  FILE *stream = fopen(path, "wb");
  if (stream == NULL) {
    snprintf(error, error_size, "failed to open certificate output");
    return false;
  }
  if (fwrite(certificate->cbor, 1U, certificate->cbor_size, stream) !=
      certificate->cbor_size) {
    fclose(stream);
    snprintf(error, error_size, "failed to write certificate");
    return false;
  }
  fclose(stream);
  return true;
}

bool etb_certificate_read_bytes(const unsigned char *bytes, size_t size,
                                etb_certificate *certificate, char *error,
                                size_t error_size) {
  etb_cbor_cursor cursor;
  size_t pair_count;
  size_t index;

  etb_certificate_init(certificate);
  certificate->cbor = (unsigned char *)malloc(size == 0U ? 1U : size);
  if (size > 0U && certificate->cbor == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }
  if (size > 0U) {
    memcpy(certificate->cbor, bytes, size);
  }
  certificate->cbor_size = size;
  etb_cbor_cursor_init(&cursor, certificate->cbor, certificate->cbor_size);
  if (!etb_cbor_read_map_header(&cursor, &pair_count)) {
    snprintf(error, error_size, "invalid certificate encoding");
    return false;
  }
  for (index = 0U; index < pair_count; ++index) {
    char *key = NULL;
    if (!etb_cbor_read_text(&cursor, &key)) {
      snprintf(error, error_size, "invalid certificate field");
      free(key);
      return false;
    }
    if (strcmp(key, "query") == 0) {
      char *query_text = NULL;
      if (!etb_cbor_read_text(&cursor, &query_text) ||
          !etb_parse_atom_text(query_text, &certificate->query, error, error_size)) {
        free(key);
        free(query_text);
        return false;
      }
      free(query_text);
    } else if (strcmp(key, "answers") == 0) {
      size_t answer_count;
      size_t answer_index;
      if (!etb_cbor_read_array_header(&cursor, &answer_count)) {
        free(key);
        snprintf(error, error_size, "invalid answers encoding");
        return false;
      }
      certificate->answers =
          (etb_atom *)calloc(answer_count == 0U ? 1U : answer_count,
                             sizeof(etb_atom));
      if (answer_count > 0U && certificate->answers == NULL) {
        free(key);
        snprintf(error, error_size, "out of memory");
        return false;
      }
      certificate->answer_count = answer_count;
      for (answer_index = 0U; answer_index < answer_count; ++answer_index) {
        char *answer_text = NULL;
        if (!etb_cbor_read_text(&cursor, &answer_text) ||
            !etb_parse_atom_text(answer_text, &certificate->answers[answer_index],
                                 error, error_size)) {
          free(answer_text);
          free(key);
          return false;
        }
        free(answer_text);
      }
    } else if (strcmp(key, "trace") == 0) {
      size_t trace_count;
      size_t trace_index;
      if (!etb_cbor_read_array_header(&cursor, &trace_count)) {
        free(key);
        snprintf(error, error_size, "invalid trace encoding");
        return false;
      }
      for (trace_index = 0U; trace_index < trace_count; ++trace_index) {
        char *digest = NULL;
        if (!etb_cbor_read_text(&cursor, &digest)) {
          free(key);
          snprintf(error, error_size, "invalid trace digest");
          return false;
        }
        free(digest);
      }
    } else if (strcmp(key, "root") == 0) {
      char *root_text = NULL;
      if (!etb_cbor_read_text(&cursor, &root_text)) {
        free(key);
        snprintf(error, error_size, "invalid root digest");
        return false;
      }
      snprintf(certificate->root_digest, sizeof(certificate->root_digest), "%s",
               root_text);
      free(root_text);
    } else {
      free(key);
      snprintf(error, error_size, "unknown certificate field");
      return false;
    }
    free(key);
  }
  return true;
}

bool etb_certificate_read_file(const char *path, etb_certificate *certificate,
                               char *error, size_t error_size) {
  FILE *stream;
  long size;
  unsigned char *bytes = NULL;
  bool ok;
  stream = fopen(path, "rb");
  if (stream == NULL) {
    snprintf(error, error_size, "failed to open certificate");
    return false;
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    fclose(stream);
    snprintf(error, error_size, "failed to seek certificate");
    return false;
  }
  size = ftell(stream);
  if (size < 0) {
    fclose(stream);
    snprintf(error, error_size, "failed to read certificate size");
    return false;
  }
  rewind(stream);
  bytes = (unsigned char *)malloc((size_t)size == 0U ? 1U : (size_t)size);
  if (size > 0 && bytes == NULL) {
    fclose(stream);
    snprintf(error, error_size, "out of memory");
    return false;
  }
  if (size > 0 &&
      fread(bytes, 1U, (size_t)size, stream) != (size_t)size) {
    fclose(stream);
    free(bytes);
    snprintf(error, error_size, "failed to read certificate");
    return false;
  }
  fclose(stream);
  ok = etb_certificate_read_bytes(bytes, (size_t)size, certificate, error,
                                  error_size);
  free(bytes);
  return ok;
}
