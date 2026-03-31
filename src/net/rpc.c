#include "../core/cbor.h"

bool etb_rpc_encode_ping(etb_cbor_buffer *buffer, const char *node_id) {
  return etb_cbor_write_array_header(buffer, 2U) &&
         etb_cbor_write_text(buffer, "ping") &&
         etb_cbor_write_text(buffer, node_id);
}
