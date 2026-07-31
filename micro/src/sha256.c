/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* SHA-256 (FIPS 180-4), the one cryptographic primitive this library carries.
 *
 * In-tree rather than injected because it is small, keyless, and checking a
 * document against its own manifest is common enough on device that a callback
 * would be ceremony - see mcdf_micro_verify.h for where the line is drawn.
 * Signature verification, when it arrives, will be injected instead.
 *
 * Written for a part with no cache and no allocator: 64-byte block at a time,
 * no tables beyond the round constants, no dynamic state. Correctness is
 * checked against the FIPS vectors and, more usefully, against digests the
 * reference runtime wrote into the conformance vectors' manifests. */

#include "mcdf_micro/mcdf_micro_verify.h"

#include "internal.h"

#ifdef MCDF_MICRO_ENABLE_INTEGRITY

static const uint32_t mm_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t mm_ror(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}

static void mm_sha256_block(uint32_t *state, const unsigned char *p) {
  uint32_t w[64], a, b, c, d, e, f, g, h;
  unsigned i;

  for (i = 0; i < 16u; ++i) {
    w[i] = ((uint32_t)p[i * 4u] << 24) | ((uint32_t)p[i * 4u + 1u] << 16) |
           ((uint32_t)p[i * 4u + 2u] << 8) | (uint32_t)p[i * 4u + 3u];
  }
  for (i = 16u; i < 64u; ++i) {
    const uint32_t s0 = mm_ror(w[i - 15u], 7) ^ mm_ror(w[i - 15u], 18) ^
                        (w[i - 15u] >> 3);
    const uint32_t s1 = mm_ror(w[i - 2u], 17) ^ mm_ror(w[i - 2u], 19) ^
                        (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  a = state[0]; b = state[1]; c = state[2]; d = state[3];
  e = state[4]; f = state[5]; g = state[6]; h = state[7];

  for (i = 0; i < 64u; ++i) {
    const uint32_t s1 = mm_ror(e, 6) ^ mm_ror(e, 11) ^ mm_ror(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + ch + mm_k[i] + w[i];
    const uint32_t s0 = mm_ror(a, 2) ^ mm_ror(a, 13) ^ mm_ror(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }

  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

mcdf_micro_status mcdf_micro_sha256_init(mcdf_micro_sha256 *ctx) {
  if (ctx == NULL) return MCDF_MICRO_E_INVAL;
  ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
  ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
  ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
  ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
  ctx->total = 0;
  ctx->fill = 0;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_sha256_update(mcdf_micro_sha256 *ctx,
                                           const void *data, size_t len) {
  const unsigned char *p = (const unsigned char *)data;
  size_t i;

  if (ctx == NULL || (data == NULL && len != 0)) return MCDF_MICRO_E_INVAL;
  ctx->total += (uint64_t)len;

  for (i = 0; i < len; ++i) {
    ctx->block[ctx->fill++] = p[i];
    if (ctx->fill == sizeof ctx->block) {
      mm_sha256_block(ctx->state, ctx->block);
      ctx->fill = 0;
    }
  }
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_sha256_final(mcdf_micro_sha256 *ctx,
                                          unsigned char *out) {
  uint64_t bits;
  size_t i;

  if (ctx == NULL || out == NULL) return MCDF_MICRO_E_INVAL;
  bits = ctx->total * 8u;

  ctx->block[ctx->fill++] = 0x80u;
  if (ctx->fill > 56u) {
    while (ctx->fill < sizeof ctx->block) ctx->block[ctx->fill++] = 0;
    mm_sha256_block(ctx->state, ctx->block);
    ctx->fill = 0;
  }
  while (ctx->fill < 56u) ctx->block[ctx->fill++] = 0;
  for (i = 0; i < 8u; ++i) {
    ctx->block[56u + i] = (unsigned char)(bits >> (56u - 8u * i));
  }
  mm_sha256_block(ctx->state, ctx->block);

  for (i = 0; i < 8u; ++i) {
    out[i * 4u]      = (unsigned char)(ctx->state[i] >> 24);
    out[i * 4u + 1u] = (unsigned char)(ctx->state[i] >> 16);
    out[i * 4u + 2u] = (unsigned char)(ctx->state[i] >> 8);
    out[i * 4u + 3u] = (unsigned char)(ctx->state[i]);
  }
  return MCDF_MICRO_OK;
}

#else /* the gate removes the code, not just the calls */

mcdf_micro_status mcdf_micro_sha256_init(mcdf_micro_sha256 *ctx) {
  (void)ctx;
  return MCDF_MICRO_E_DISABLED;
}

mcdf_micro_status mcdf_micro_sha256_update(mcdf_micro_sha256 *ctx,
                                           const void *data, size_t len) {
  (void)ctx; (void)data; (void)len;
  return MCDF_MICRO_E_DISABLED;
}

mcdf_micro_status mcdf_micro_sha256_final(mcdf_micro_sha256 *ctx,
                                          unsigned char *out) {
  (void)ctx; (void)out;
  return MCDF_MICRO_E_DISABLED;
}

#endif /* MCDF_MICRO_ENABLE_INTEGRITY */
