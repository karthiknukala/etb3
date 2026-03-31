#include "cbor.h"

#include <stdlib.h>
#include <string.h>

static bool etb_cbor_reserve(etb_cbor_buffer *buffer, size_t extra) {
  unsigned char *data;
  size_t capacity;
  if (buffer->size + extra <= buffer->capacity) {
    return true;
  }
  capacity = buffer->capacity == 0U ? 128U : buffer->capacity * 2U;
  while (capacity < buffer->size + extra) {
    capacity *= 2U;
  }
  data = (unsigned char *)realloc(buffer->data, capacity);
  if (data == NULL) {
    return false;
  }
  buffer->data = data;
  buffer->capacity = capacity;
  return true;
}

static bool etb_cbor_write_type_value(etb_cbor_buffer *buffer, unsigned char major,
                                      uint64_t value) {
  if (value <= 23U) {
    if (!etb_cbor_reserve(buffer, 1U)) {
      return false;
    }
    buffer->data[buffer->size++] = (unsigned char)(major | value);
    return true;
  }
  if (value <= 0xffU) {
    if (!etb_cbor_reserve(buffer, 2U)) {
      return false;
    }
    buffer->data[buffer->size++] = (unsigned char)(major | 24U);
    buffer->data[buffer->size++] = (unsigned char)value;
    return true;
  }
  if (value <= 0xffffU) {
    if (!etb_cbor_reserve(buffer, 3U)) {
      return false;
    }
    buffer->data[buffer->size++] = (unsigned char)(major | 25U);
    buffer->data[buffer->size++] = (unsigned char)(value >> 8U);
    buffer->data[buffer->size++] = (unsigned char)value;
    return true;
  }
  if (value <= 0xffffffffU) {
    if (!etb_cbor_reserve(buffer, 5U)) {
      return false;
    }
    buffer->data[buffer->size++] = (unsigned char)(major | 26U);
    buffer->data[buffer->size++] = (unsigned char)(value >> 24U);
    buffer->data[buffer->size++] = (unsigned char)(value >> 16U);
    buffer->data[buffer->size++] = (unsigned char)(value >> 8U);
    buffer->data[buffer->size++] = (unsigned char)value;
    return true;
  }
  if (!etb_cbor_reserve(buffer, 9U)) {
    return false;
  }
  buffer->data[buffer->size++] = (unsigned char)(major | 27U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 56U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 48U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 40U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 32U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 24U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 16U);
  buffer->data[buffer->size++] = (unsigned char)(value >> 8U);
  buffer->data[buffer->size++] = (unsigned char)value;
  return true;
}

static bool etb_cbor_read_type_value(etb_cbor_cursor *cursor,
                                     unsigned char expected_major,
                                     uint64_t *value) {
  unsigned char head;
  unsigned char major;
  unsigned char addl;
  size_t bytes;
  uint64_t result = 0U;

  if (cursor->offset >= cursor->size) {
    return false;
  }
  head = cursor->data[cursor->offset++];
  major = head & 0xe0U;
  addl = head & 0x1fU;
  if (major != expected_major) {
    return false;
  }
  if (addl <= 23U) {
    *value = addl;
    return true;
  }
  bytes = addl == 24U ? 1U : addl == 25U ? 2U : addl == 26U ? 4U : addl == 27U ? 8U : 0U;
  if (bytes == 0U || cursor->offset + bytes > cursor->size) {
    return false;
  }
  while (bytes-- > 0U) {
    result = (result << 8U) | cursor->data[cursor->offset++];
  }
  *value = result;
  return true;
}

void etb_cbor_buffer_init(etb_cbor_buffer *buffer) {
  buffer->data = NULL;
  buffer->size = 0U;
  buffer->capacity = 0U;
}

void etb_cbor_buffer_free(etb_cbor_buffer *buffer) {
  free(buffer->data);
  buffer->data = NULL;
  buffer->size = 0U;
  buffer->capacity = 0U;
}

bool etb_cbor_write_uint(etb_cbor_buffer *buffer, uint64_t value) {
  return etb_cbor_write_type_value(buffer, 0x00U, value);
}

bool etb_cbor_write_int(etb_cbor_buffer *buffer, int64_t value) {
  if (value >= 0) {
    return etb_cbor_write_uint(buffer, (uint64_t)value);
  }
  return etb_cbor_write_type_value(buffer, 0x20U, (uint64_t)(-1 - value));
}

bool etb_cbor_write_bool(etb_cbor_buffer *buffer, bool value) {
  if (!etb_cbor_reserve(buffer, 1U)) {
    return false;
  }
  buffer->data[buffer->size++] = value ? 0xf5U : 0xf4U;
  return true;
}

bool etb_cbor_write_null(etb_cbor_buffer *buffer) {
  if (!etb_cbor_reserve(buffer, 1U)) {
    return false;
  }
  buffer->data[buffer->size++] = 0xf6U;
  return true;
}

bool etb_cbor_write_text(etb_cbor_buffer *buffer, const char *value) {
  size_t size = strlen(value);
  if (!etb_cbor_write_type_value(buffer, 0x60U, size) ||
      !etb_cbor_reserve(buffer, size)) {
    return false;
  }
  memcpy(buffer->data + buffer->size, value, size);
  buffer->size += size;
  return true;
}

bool etb_cbor_write_bytes(etb_cbor_buffer *buffer, const unsigned char *value,
                          size_t size) {
  if (!etb_cbor_write_type_value(buffer, 0x40U, size) ||
      !etb_cbor_reserve(buffer, size)) {
    return false;
  }
  memcpy(buffer->data + buffer->size, value, size);
  buffer->size += size;
  return true;
}

bool etb_cbor_write_array_header(etb_cbor_buffer *buffer, size_t count) {
  return etb_cbor_write_type_value(buffer, 0x80U, count);
}

bool etb_cbor_write_map_header(etb_cbor_buffer *buffer, size_t count) {
  return etb_cbor_write_type_value(buffer, 0xa0U, count);
}

void etb_cbor_cursor_init(etb_cbor_cursor *cursor, const unsigned char *data,
                          size_t size) {
  cursor->data = data;
  cursor->size = size;
  cursor->offset = 0U;
}

bool etb_cbor_read_uint(etb_cbor_cursor *cursor, uint64_t *value) {
  return etb_cbor_read_type_value(cursor, 0x00U, value);
}

bool etb_cbor_read_int(etb_cbor_cursor *cursor, int64_t *value) {
  unsigned char head;
  unsigned char major;
  unsigned char addl;
  uint64_t raw;
  if (cursor->offset >= cursor->size) {
    return false;
  }
  head = cursor->data[cursor->offset];
  major = head & 0xe0U;
  if (major == 0x00U) {
    if (!etb_cbor_read_uint(cursor, &raw)) {
      return false;
    }
    *value = (int64_t)raw;
    return true;
  }
  if (major != 0x20U) {
    return false;
  }
  addl = head & 0x1fU;
  (void)addl;
  if (!etb_cbor_read_type_value(cursor, 0x20U, &raw)) {
    return false;
  }
  *value = -(int64_t)(raw + 1U);
  return true;
}

bool etb_cbor_read_bool(etb_cbor_cursor *cursor, bool *value) {
  unsigned char byte;
  if (cursor->offset >= cursor->size) {
    return false;
  }
  byte = cursor->data[cursor->offset++];
  if (byte == 0xf4U) {
    *value = false;
    return true;
  }
  if (byte == 0xf5U) {
    *value = true;
    return true;
  }
  return false;
}

bool etb_cbor_read_null(etb_cbor_cursor *cursor) {
  if (cursor->offset >= cursor->size) {
    return false;
  }
  if (cursor->data[cursor->offset] != 0xf6U) {
    return false;
  }
  cursor->offset += 1U;
  return true;
}

bool etb_cbor_read_text(etb_cbor_cursor *cursor, char **value) {
  uint64_t size;
  char *text;
  if (!etb_cbor_read_type_value(cursor, 0x60U, &size) ||
      cursor->offset + (size_t)size > cursor->size) {
    return false;
  }
  text = (char *)malloc((size_t)size + 1U);
  if (text == NULL) {
    return false;
  }
  memcpy(text, cursor->data + cursor->offset, (size_t)size);
  text[size] = '\0';
  cursor->offset += (size_t)size;
  *value = text;
  return true;
}

bool etb_cbor_read_bytes(etb_cbor_cursor *cursor, unsigned char **value,
                         size_t *size_out) {
  uint64_t size;
  unsigned char *bytes;
  if (!etb_cbor_read_type_value(cursor, 0x40U, &size) ||
      cursor->offset + (size_t)size > cursor->size) {
    return false;
  }
  bytes = (unsigned char *)malloc(size == 0U ? 1U : (size_t)size);
  if (size > 0U && bytes == NULL) {
    return false;
  }
  if (size > 0U) {
    memcpy(bytes, cursor->data + cursor->offset, (size_t)size);
  }
  cursor->offset += (size_t)size;
  *value = bytes;
  *size_out = (size_t)size;
  return true;
}

bool etb_cbor_read_array_header(etb_cbor_cursor *cursor, size_t *count) {
  uint64_t size;
  if (!etb_cbor_read_type_value(cursor, 0x80U, &size)) {
    return false;
  }
  *count = (size_t)size;
  return true;
}

bool etb_cbor_read_map_header(etb_cbor_cursor *cursor, size_t *count) {
  uint64_t size;
  if (!etb_cbor_read_type_value(cursor, 0xa0U, &size)) {
    return false;
  }
  *count = (size_t)size;
  return true;
}
