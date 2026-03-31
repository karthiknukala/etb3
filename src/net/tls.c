#include <stdbool.h>
#include <stdio.h>

bool etb_tls_available(void) { return false; }

const char *etb_tls_status_message(void) {
  return "TLS transport scaffolding is present but socket-layer TLS is not implemented in this turn";
}
