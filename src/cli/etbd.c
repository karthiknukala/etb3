#include "etb/cert.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool etb_daemon_run_local(const char *program_path, const char *query_text,
                          etb_certificate *certificate, char *error,
                          size_t error_size);

int main(int argc, char **argv) {
  etb_certificate certificate;
  char error[256];
  size_t index;
  if (argc < 3) {
    fprintf(stderr, "usage: %s PROGRAM QUERY\n", argv[0]);
    return 1;
  }
  memset(error, 0, sizeof(error));
  etb_certificate_init(&certificate);
  if (!etb_daemon_run_local(argv[1], argv[2], &certificate, error, sizeof(error))) {
    fprintf(stderr, "etbd: %s\n", error);
    return 1;
  }
  printf("root=%s\n", certificate.root_digest);
  for (index = 0U; index < certificate.answer_count; ++index) {
    char *text = NULL;
    extern bool etb_atom_canonical_text(const etb_atom *atom, char **text);
    if (etb_atom_canonical_text(&certificate.answers[index], &text)) {
      printf("answer %zu: %s\n", index + 1U, text);
      free(text);
    }
  }
  return 0;
}
