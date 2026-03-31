#ifndef ETB_CBOR_H
#define ETB_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct etb_cbor_buffer {
  unsigned char *data;
  size_t size;
  size_t capacity;
} etb_cbor_buffer;

typedef struct etb_cbor_cursor {
  const unsigned char *data;
  size_t size;
  size_t offset;
} etb_cbor_cursor;

void etb_cbor_buffer_init(etb_cbor_buffer *buffer);
void etb_cbor_buffer_free(etb_cbor_buffer *buffer);
bool etb_cbor_write_uint(etb_cbor_buffer *buffer, uint64_t value);
bool etb_cbor_write_int(etb_cbor_buffer *buffer, int64_t value);
bool etb_cbor_write_bool(etb_cbor_buffer *buffer, bool value);
bool etb_cbor_write_null(etb_cbor_buffer *buffer);
bool etb_cbor_write_text(etb_cbor_buffer *buffer, const char *value);
bool etb_cbor_write_bytes(etb_cbor_buffer *buffer, const unsigned char *value,
                          size_t size);
bool etb_cbor_write_array_header(etb_cbor_buffer *buffer, size_t count);
bool etb_cbor_write_map_header(etb_cbor_buffer *buffer, size_t count);

void etb_cbor_cursor_init(etb_cbor_cursor *cursor, const unsigned char *data,
                          size_t size);
bool etb_cbor_read_uint(etb_cbor_cursor *cursor, uint64_t *value);
bool etb_cbor_read_int(etb_cbor_cursor *cursor, int64_t *value);
bool etb_cbor_read_bool(etb_cbor_cursor *cursor, bool *value);
bool etb_cbor_read_null(etb_cbor_cursor *cursor);
bool etb_cbor_read_text(etb_cbor_cursor *cursor, char **value);
bool etb_cbor_read_array_header(etb_cbor_cursor *cursor, size_t *count);
bool etb_cbor_read_map_header(etb_cbor_cursor *cursor, size_t *count);

#endif
