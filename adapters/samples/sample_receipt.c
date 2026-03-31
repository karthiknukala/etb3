#include "../../include/etb/term.h"
#include "../../src/core/cbor.h"
#include "../../src/core/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_term(etb_cbor_cursor *cursor, etb_term *term) {
  size_t count;
  uint64_t kind;
  char *text = NULL;
  if (!etb_cbor_read_array_header(cursor, &count) || count != 2U ||
      !etb_cbor_read_uint(cursor, &kind)) return false;
  if (kind == ETB_TERM_NULL) {
    if (!etb_cbor_read_null(cursor)) return false;
    *term = etb_term_make_null();
    return true;
  }
  if (!etb_cbor_read_text(cursor, &text)) return false;
  *term = etb_term_make_string(text);
  free(text);
  return true;
}

static bool write_term(etb_cbor_buffer *buffer, const etb_term *term) {
  return etb_cbor_write_array_header(buffer, 2U) &&
         etb_cbor_write_uint(buffer, term->kind) &&
         (term->kind == ETB_TERM_NULL ? etb_cbor_write_null(buffer)
                                      : etb_cbor_write_text(buffer, term->text));
}

int main(void) {
  unsigned char request[4096];
  size_t size = fread(request, 1U, sizeof(request), stdin);
  etb_cbor_cursor cursor;
  etb_term arg;
  size_t outer;
  size_t count;
  char digest[65];
  etb_cbor_buffer out;

  etb_cbor_cursor_init(&cursor, request, size);
  if (!etb_cbor_read_array_header(&cursor, &outer) || outer != 2U ||
      !etb_cbor_read_array_header(&cursor, &count) || count != 2U ||
      !read_term(&cursor, &arg) || !read_term(&cursor, &arg)) {
    return 2;
  }
  etb_sha256_hex((const unsigned char *)(arg.text == NULL ? "" : arg.text),
                 arg.text == NULL ? 0U : strlen(arg.text), digest);
  etb_cbor_buffer_init(&out);
  etb_cbor_write_array_header(&out, 2U);
  etb_cbor_write_array_header(&out, 1U);
  etb_cbor_write_array_header(&out, 2U);
  write_term(&out, &arg);
  write_term(&out, &(etb_term){.kind = ETB_TERM_STRING, .text = "ok"});
  etb_cbor_write_array_header(&out, 1U);
  etb_cbor_write_text(&out, digest);
  fwrite(out.data, 1U, out.size, stdout);
  etb_cbor_buffer_free(&out);
  etb_term_free(&arg);
  return 0;
}
