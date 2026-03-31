#ifndef ETB_SHA256_H
#define ETB_SHA256_H

#include <stddef.h>
#include <stdint.h>

void etb_sha256(const unsigned char *data, size_t size,
                unsigned char digest[32]);
void etb_sha256_hex(const unsigned char *data, size_t size, char hex[65]);

#endif
