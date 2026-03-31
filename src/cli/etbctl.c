#include "etb/cert.h"
#include "etb/distributed.h"

#include "../core/canon.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void sanitize_name(const char *input, char *output, size_t output_size) {
  size_t index = 0U;
  while (*input != '\0' && index + 1U < output_size) {
    output[index++] =
        isalnum((unsigned char)*input) != 0 ? *input : '_';
    input += 1;
  }
  output[index] = '\0';
}

static bool ensure_directory(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode) != 0;
  }
  return mkdir(path, 0700) == 0;
}

static bool write_bytes(const char *path, const unsigned char *bytes,
                        size_t size) {
  FILE *stream = fopen(path, "wb");
  if (stream == NULL) {
    return false;
  }
  if (size > 0U && fwrite(bytes, 1U, size, stream) != size) {
    fclose(stream);
    return false;
  }
  fclose(stream);
  return true;
}

static bool write_text(const char *path, const char *text) {
  FILE *stream = fopen(path, "wb");
  size_t size;
  if (stream == NULL) {
    return false;
  }
  size = text == NULL ? 0U : strlen(text);
  if (size > 0U && fwrite(text, 1U, size, stream) != size) {
    fclose(stream);
    return false;
  }
  fclose(stream);
  return true;
}

static bool write_bundle_dir(const char *dir, const etb_bundle_list *bundles) {
  size_t index;
  if (!ensure_directory(dir)) {
    return false;
  }
  for (index = 0U; index < bundles->count; ++index) {
    char node_name[128];
    char cert_path[512];
    char proof_path[512];
    char trace_path[512];
    sanitize_name(bundles->items[index].node_id, node_name, sizeof(node_name));
    snprintf(cert_path, sizeof(cert_path), "%s/%02zu-%s.cert.cbor", dir,
             index + 1U, node_name);
    if (!write_bytes(cert_path, bundles->items[index].certificate_bytes,
                     bundles->items[index].certificate_size)) {
      return false;
    }
    if (bundles->items[index].proof_size > 0U) {
      snprintf(proof_path, sizeof(proof_path), "%s/%02zu-%s.proof", dir,
               index + 1U, node_name);
      if (!write_bytes(proof_path, bundles->items[index].proof_bytes,
                       bundles->items[index].proof_size)) {
        return false;
      }
    }
    if (bundles->items[index].trace_text != NULL) {
      snprintf(trace_path, sizeof(trace_path), "%s/%02zu-%s.trace.txt", dir,
               index + 1U, node_name);
      if (!write_text(trace_path, bundles->items[index].trace_text)) {
        return false;
      }
    }
  }
  return true;
}

int main(int argc, char **argv) {
  const char *endpoint;
  const char *query_text;
  const char *cert_out = NULL;
  const char *proof_out = NULL;
  const char *bundle_dir = NULL;
  const char *prover_path = "./adapters/zk-trace-check/target/debug/zk-trace-check";
  int verify_proof = 0;
  size_t index;
  char error[256];
  etb_bundle_list bundles;
  etb_certificate certificate;

  if (argc < 4 || strcmp(argv[1], "query") != 0) {
    fprintf(stderr,
            "usage: %s query HOST:PORT QUERY [--cert-out FILE] [--proof-out FILE] "
            "[--bundle-dir DIR] [--verify-proof] [--prover PATH]\n",
            argv[0]);
    return 1;
  }
  endpoint = argv[2];
  query_text = argv[3];
  for (index = 4U; index < (size_t)argc; ++index) {
    if (strcmp(argv[index], "--cert-out") == 0 && index + 1U < (size_t)argc) {
      cert_out = argv[++index];
    } else if (strcmp(argv[index], "--proof-out") == 0 &&
               index + 1U < (size_t)argc) {
      proof_out = argv[++index];
    } else if (strcmp(argv[index], "--bundle-dir") == 0 &&
               index + 1U < (size_t)argc) {
      bundle_dir = argv[++index];
    } else if (strcmp(argv[index], "--verify-proof") == 0) {
      verify_proof = 1;
    } else if (strcmp(argv[index], "--prover") == 0 &&
               index + 1U < (size_t)argc) {
      prover_path = argv[++index];
    } else {
      fprintf(stderr, "etbctl: unknown or incomplete option '%s'\n", argv[index]);
      return 1;
    }
  }
  if (proof_out != NULL && cert_out == NULL) {
    fprintf(stderr, "etbctl: --proof-out requires --cert-out\n");
    return 1;
  }

  memset(error, 0, sizeof(error));
  etb_bundle_list_init(&bundles);
  etb_certificate_init(&certificate);
  if (!etb_remote_query(endpoint, query_text, &bundles, error, sizeof(error))) {
    fprintf(stderr, "etbctl: %s\n", error);
    etb_bundle_list_free(&bundles);
    return 1;
  }
  if (verify_proof) {
    for (index = 0U; index < bundles.count; ++index) {
      if (!etb_bundle_verify_with_prover(prover_path, &bundles.items[index], error,
                                         sizeof(error))) {
        fprintf(stderr, "etbctl: %s\n", error);
        etb_bundle_list_free(&bundles);
        return 1;
      }
    }
  }
  if (bundles.count == 0U ||
      !etb_certificate_read_bytes(bundles.items[bundles.count - 1U].certificate_bytes,
                                  bundles.items[bundles.count - 1U].certificate_size,
                                  &certificate, error, sizeof(error))) {
    fprintf(stderr, "etbctl: %s\n", error[0] == '\0' ? "missing top-level bundle"
                                                     : error);
    etb_certificate_free(&certificate);
    etb_bundle_list_free(&bundles);
    return 1;
  }
  if (cert_out != NULL &&
      !write_bytes(cert_out, bundles.items[bundles.count - 1U].certificate_bytes,
                   bundles.items[bundles.count - 1U].certificate_size)) {
    fprintf(stderr, "etbctl: failed to write %s\n", cert_out);
    etb_certificate_free(&certificate);
    etb_bundle_list_free(&bundles);
    return 1;
  }
  if (proof_out != NULL &&
      !write_bytes(proof_out, bundles.items[bundles.count - 1U].proof_bytes,
                   bundles.items[bundles.count - 1U].proof_size)) {
    fprintf(stderr, "etbctl: failed to write %s\n", proof_out);
    etb_certificate_free(&certificate);
    etb_bundle_list_free(&bundles);
    return 1;
  }
  if (bundle_dir != NULL && !write_bundle_dir(bundle_dir, &bundles)) {
    fprintf(stderr, "etbctl: failed to write bundle directory %s\n", bundle_dir);
    etb_certificate_free(&certificate);
    etb_bundle_list_free(&bundles);
    return 1;
  }

  printf("root=%s\n", certificate.root_digest);
  for (index = 0U; index < certificate.answer_count; ++index) {
    char *text = NULL;
    if (etb_atom_canonical_text(&certificate.answers[index], &text)) {
      printf("answer %zu: %s\n", index + 1U, text);
      free(text);
    }
  }
  printf("bundles=%zu\n", bundles.count);
  if (cert_out != NULL) {
    printf("certificate=%s\n", cert_out);
  }
  if (proof_out != NULL) {
    printf("proof=%s\n", proof_out);
  }
  if (bundle_dir != NULL) {
    printf("bundle_dir=%s\n", bundle_dir);
  }

  etb_certificate_free(&certificate);
  etb_bundle_list_free(&bundles);
  return 0;
}
