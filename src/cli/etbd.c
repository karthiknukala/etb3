#include "etb/cert.h"

#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

extern char **environ;

bool etb_daemon_run_local(const char *program_path, const char *query_text,
                          etb_certificate *certificate, char *error,
                          size_t error_size);
bool etb_daemon_run_local_with_imports(const char *program_path,
                                       const char *query_text,
                                       const char *const *import_paths,
                                       size_t import_count,
                                       etb_certificate *certificate,
                                       char *error, size_t error_size);

static bool run_prover(const char *prover_path, const char *command,
                       const char *cert_path, const char *proof_path,
                       char *error, size_t error_size) {
  pid_t pid;
  int status;
  char *const argv[] = {(char *)prover_path, (char *)command, (char *)cert_path,
                        (char *)proof_path, NULL};
  if (posix_spawn(&pid, prover_path, NULL, NULL, argv, environ) != 0) {
    snprintf(error, error_size, "failed to launch prover %s", prover_path);
    return false;
  }
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0) {
    snprintf(error, error_size, "prover command '%s' failed", command);
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  etb_certificate certificate;
  char error[256];
  size_t index;
  const char *program_path;
  const char *query_text;
  const char *cert_out = NULL;
  const char *proof_out = NULL;
  const char *prover_path = "./adapters/zk-trace-check/target/debug/zk-trace-check";
  const char *import_paths[32];
  size_t import_count = 0U;
  int verify_proof = 0;

  if (argc < 3) {
    fprintf(stderr,
            "usage: %s PROGRAM QUERY [--import-cert FILE ...] [--cert-out FILE] "
            "[--proof-out FILE] [--prover PATH] [--verify-proof]\n",
            argv[0]);
    return 1;
  }
  program_path = argv[1];
  query_text = argv[2];
  for (index = 3U; index < (size_t)argc; ++index) {
    if (strcmp(argv[index], "--import-cert") == 0 && index + 1U < (size_t)argc) {
      import_paths[import_count++] = argv[++index];
    } else if (strcmp(argv[index], "--cert-out") == 0 &&
               index + 1U < (size_t)argc) {
      cert_out = argv[++index];
    } else if (strcmp(argv[index], "--proof-out") == 0 &&
               index + 1U < (size_t)argc) {
      proof_out = argv[++index];
    } else if (strcmp(argv[index], "--prover") == 0 &&
               index + 1U < (size_t)argc) {
      prover_path = argv[++index];
    } else if (strcmp(argv[index], "--verify-proof") == 0) {
      verify_proof = 1;
    } else {
      fprintf(stderr, "etbd: unknown or incomplete option '%s'\n", argv[index]);
      return 1;
    }
  }
  if (proof_out != NULL && cert_out == NULL) {
    fprintf(stderr, "etbd: --proof-out requires --cert-out\n");
    return 1;
  }
  memset(error, 0, sizeof(error));
  etb_certificate_init(&certificate);
  if (!etb_daemon_run_local_with_imports(program_path, query_text, import_paths,
                                         import_count, &certificate, error,
                                         sizeof(error))) {
    fprintf(stderr, "etbd: %s\n", error);
    return 1;
  }
  if (cert_out != NULL &&
      !etb_certificate_write_file(&certificate, cert_out, error, sizeof(error))) {
    fprintf(stderr, "etbd: %s\n", error);
    return 1;
  }
  if (proof_out != NULL &&
      !run_prover(prover_path, "prove", cert_out, proof_out, error,
                  sizeof(error))) {
    fprintf(stderr, "etbd: %s\n", error);
    return 1;
  }
  if (verify_proof && proof_out != NULL &&
      !run_prover(prover_path, "verify", cert_out, proof_out, error,
                  sizeof(error))) {
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
  if (cert_out != NULL) {
    printf("certificate=%s\n", cert_out);
  }
  if (proof_out != NULL) {
    printf("proof=%s\n", proof_out);
  }
  return 0;
}
