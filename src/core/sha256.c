#include "sha256.h"

#include <stdint.h>
#include <string.h>

typedef struct etb_sha256_ctx {
  uint32_t state[8];
  uint64_t bit_count;
  unsigned char buffer[64];
  size_t buffer_size;
} etb_sha256_ctx;

static const uint32_t ETB_SHA256_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static uint32_t etb_rotr(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32U - count));
}

static void etb_sha256_transform(etb_sha256_ctx *ctx,
                                 const unsigned char block[64]) {
  uint32_t w[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t s0;
  uint32_t s1;
  uint32_t ch;
  uint32_t maj;
  uint32_t temp1;
  uint32_t temp2;
  size_t index;

  for (index = 0U; index < 16U; ++index) {
    w[index] = ((uint32_t)block[index * 4U] << 24U) |
               ((uint32_t)block[index * 4U + 1U] << 16U) |
               ((uint32_t)block[index * 4U + 2U] << 8U) |
               (uint32_t)block[index * 4U + 3U];
  }
  for (index = 16U; index < 64U; ++index) {
    s0 = etb_rotr(w[index - 15U], 7U) ^ etb_rotr(w[index - 15U], 18U) ^
         (w[index - 15U] >> 3U);
    s1 = etb_rotr(w[index - 2U], 17U) ^ etb_rotr(w[index - 2U], 19U) ^
         (w[index - 2U] >> 10U);
    w[index] = w[index - 16U] + s0 + w[index - 7U] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (index = 0U; index < 64U; ++index) {
    s1 = etb_rotr(e, 6U) ^ etb_rotr(e, 11U) ^ etb_rotr(e, 25U);
    ch = (e & f) ^ ((~e) & g);
    temp1 = h + s1 + ch + ETB_SHA256_K[index] + w[index];
    s0 = etb_rotr(a, 2U) ^ etb_rotr(a, 13U) ^ etb_rotr(a, 22U);
    maj = (a & b) ^ (a & c) ^ (b & c);
    temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void etb_sha256_init_ctx(etb_sha256_ctx *ctx) {
  static const uint32_t iv[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                 0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                 0x1f83d9abU, 0x5be0cd19U};
  memcpy(ctx->state, iv, sizeof(iv));
  ctx->bit_count = 0U;
  ctx->buffer_size = 0U;
}

static void etb_sha256_update(etb_sha256_ctx *ctx, const unsigned char *data,
                              size_t size) {
  size_t index = 0U;
  while (index < size) {
    size_t to_copy = 64U - ctx->buffer_size;
    if (to_copy > size - index) {
      to_copy = size - index;
    }
    memcpy(ctx->buffer + ctx->buffer_size, data + index, to_copy);
    ctx->buffer_size += to_copy;
    index += to_copy;
    if (ctx->buffer_size == 64U) {
      etb_sha256_transform(ctx, ctx->buffer);
      ctx->bit_count += 512U;
      ctx->buffer_size = 0U;
    }
  }
}

static void etb_sha256_final(etb_sha256_ctx *ctx, unsigned char digest[32]) {
  uint64_t total_bits;
  size_t index;

  total_bits = ctx->bit_count + (uint64_t)ctx->buffer_size * 8U;
  ctx->buffer[ctx->buffer_size++] = 0x80U;
  if (ctx->buffer_size > 56U) {
    while (ctx->buffer_size < 64U) {
      ctx->buffer[ctx->buffer_size++] = 0U;
    }
    etb_sha256_transform(ctx, ctx->buffer);
    ctx->buffer_size = 0U;
  }
  while (ctx->buffer_size < 56U) {
    ctx->buffer[ctx->buffer_size++] = 0U;
  }
  for (index = 0U; index < 8U; ++index) {
    ctx->buffer[56U + index] =
        (unsigned char)(total_bits >> ((7U - index) * 8U));
  }
  etb_sha256_transform(ctx, ctx->buffer);

  for (index = 0U; index < 8U; ++index) {
    digest[index * 4U] = (unsigned char)(ctx->state[index] >> 24U);
    digest[index * 4U + 1U] = (unsigned char)(ctx->state[index] >> 16U);
    digest[index * 4U + 2U] = (unsigned char)(ctx->state[index] >> 8U);
    digest[index * 4U + 3U] = (unsigned char)(ctx->state[index]);
  }
}

void etb_sha256(const unsigned char *data, size_t size,
                unsigned char digest[32]) {
  etb_sha256_ctx ctx;
  etb_sha256_init_ctx(&ctx);
  etb_sha256_update(&ctx, data, size);
  etb_sha256_final(&ctx, digest);
}

void etb_sha256_hex(const unsigned char *data, size_t size, char hex[65]) {
  static const char LUT[] = "0123456789abcdef";
  unsigned char digest[32];
  size_t index;

  etb_sha256(data, size, digest);
  for (index = 0U; index < 32U; ++index) {
    hex[index * 2U] = LUT[digest[index] >> 4U];
    hex[index * 2U + 1U] = LUT[digest[index] & 0x0fU];
  }
  hex[64] = '\0';
}
