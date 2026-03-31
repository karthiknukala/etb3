#include "../core/cbor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool etb_frame_encode(const unsigned char *payload, size_t payload_size,
                      unsigned char **frame, size_t *frame_size) {
  *frame_size = payload_size + 4U;
  *frame = (unsigned char *)malloc(*frame_size);
  if (*frame == NULL) {
    return false;
  }
  (*frame)[0] = (unsigned char)(payload_size >> 24U);
  (*frame)[1] = (unsigned char)(payload_size >> 16U);
  (*frame)[2] = (unsigned char)(payload_size >> 8U);
  (*frame)[3] = (unsigned char)payload_size;
  memcpy(*frame + 4U, payload, payload_size);
  return true;
}
