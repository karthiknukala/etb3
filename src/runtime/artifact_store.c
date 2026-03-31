#include "../core/sha256.h"

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool etb_artifact_store_put(const char *root, const unsigned char *data, size_t size,
                            char digest_hex[65], char *path_out,
                            size_t path_out_size) {
  char dir[1024];
  char path[1200];
  char base[1024];
  FILE *stream;
  etb_sha256_hex(data, size, digest_hex);
  snprintf(base, sizeof(base), "%s/.etb", root);
  snprintf(dir, sizeof(dir), "%s/store/sha256", base);
  mkdir(base, 0755);
  snprintf(path, sizeof(path), "%s/store", base);
  mkdir(path, 0755);
  mkdir(dir, 0755);
  snprintf(path, sizeof(path), "%s/%s", dir, digest_hex);
  stream = fopen(path, "wb");
  if (stream == NULL) {
    return false;
  }
  if (fwrite(data, 1U, size, stream) != size) {
    fclose(stream);
    remove(path);
    return false;
  }
  fclose(stream);
  if (path_out != NULL && path_out_size > 0U) {
    snprintf(path_out, path_out_size, "%s", path);
  }
  return true;
}
