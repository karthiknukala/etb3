#include "../../include/etb/term.h"
#include "../../src/core/cbor.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_term(etb_cbor_cursor *cursor, etb_term *term) {
  size_t count;
  uint64_t kind;
  char *text = NULL;
  int64_t number;
  if (!etb_cbor_read_array_header(cursor, &count) || count != 2U ||
      !etb_cbor_read_uint(cursor, &kind)) {
    return false;
  }
  if (kind == ETB_TERM_INTEGER) {
    if (!etb_cbor_read_int(cursor, &number)) return false;
    *term = etb_term_make_integer(number);
    return true;
  }
  if (kind == ETB_TERM_NULL) {
    if (!etb_cbor_read_null(cursor)) return false;
    *term = etb_term_make_null();
    return true;
  }
  if (!etb_cbor_read_text(cursor, &text)) {
    return false;
  }
  *term = (kind == ETB_TERM_STRING) ? etb_term_make_string(text)
                                    : etb_term_make_symbol(text);
  free(text);
  return true;
}

static bool write_term(etb_cbor_buffer *buffer, const etb_term *term) {
  if (!etb_cbor_write_array_header(buffer, 2U) ||
      !etb_cbor_write_uint(buffer, term->kind)) {
    return false;
  }
  if (term->kind == ETB_TERM_INTEGER) return etb_cbor_write_int(buffer, term->integer);
  if (term->kind == ETB_TERM_NULL) return etb_cbor_write_null(buffer);
  return etb_cbor_write_text(buffer, term->text == NULL ? "" : term->text);
}

int main(void) {
  unsigned char request[4096];
  size_t size = fread(request, 1U, sizeof(request), stdin);
  etb_cbor_cursor cursor;
  etb_term args[3];
  bool bound[3];
  size_t outer;
  size_t count;
  etb_cbor_buffer out;
  char joined[512];
  etb_term result[3];

  etb_cbor_cursor_init(&cursor, request, size);
  if (!etb_cbor_read_array_header(&cursor, &outer) || outer != 2U ||
      !etb_cbor_read_array_header(&cursor, &count) || count != 3U) {
    return 2;
  }
  for (size_t i = 0U; i < 3U; ++i) {
    if (!read_term(&cursor, &args[i])) return 2;
  }
  if (!etb_cbor_read_array_header(&cursor, &count) || count != 3U) return 2;
  for (size_t i = 0U; i < 3U; ++i) {
    if (!etb_cbor_read_bool(&cursor, &bound[i])) return 2;
  }

  snprintf(joined, sizeof(joined), "%s%s", args[0].text == NULL ? "" : args[0].text,
           args[1].text == NULL ? "" : args[1].text);
  result[0] = args[0];
  result[1] = args[1];
  result[2] = etb_term_make_string(joined);

  etb_cbor_buffer_init(&out);
  etb_cbor_write_array_header(&out, 2U);
  etb_cbor_write_array_header(&out, 1U);
  etb_cbor_write_array_header(&out, 3U);
  for (size_t i = 0U; i < 3U; ++i) {
    write_term(&out, &result[i]);
  }
  etb_cbor_write_array_header(&out, 0U);
  fwrite(out.data, 1U, out.size, stdout);
  etb_cbor_buffer_free(&out);
  etb_term_free(&result[2]);
  return 0;
}
