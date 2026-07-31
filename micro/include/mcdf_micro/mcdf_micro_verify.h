/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */
#ifndef MCDF_MICRO_VERIFY_H
#define MCDF_MICRO_VERIFY_H

/*
 * mcdf_micro - the Integrity profile: SHA-256 over the members, compared to
 * manifest.json.
 *
 * SHA-256 is the one cryptographic primitive this library carries in-tree.
 * Everything else the profile ladder needs above Core is injected: signature
 * verification will arrive as a caller-supplied callback, because a part with
 * no ES256 support should simply not register one, and signing, key generation
 * and decryption never arrive at all - a reader holds no private keys.
 * Hashing is the exception because it is small, has no key material, and
 * checking a document against its own manifest is common enough on device that
 * a callback for it would be ceremony.
 *
 * The whole file is behind MCDF_MICRO_ENABLE_INTEGRITY. When that gate is off
 * the declarations stay - so one piece of caller code compiles against every
 * configuration - and the bodies return MCDF_MICRO_E_DISABLED. Callers that
 * want to branch at compile time test MCDF_MICRO_HAS_INTEGRITY.
 */

#include <stddef.h>
#include <stdint.h>

#include "mcdf_micro/mcdf_micro.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MCDF_MICRO_ENABLE_INTEGRITY
#define MCDF_MICRO_HAS_INTEGRITY 1
#endif

/* ---------------------------------------------------------------- sha-256 */

#define MCDF_MICRO_SHA256_SIZE     32u
#define MCDF_MICRO_SHA256_HEX_SIZE 65u /* 64 hex digits plus the NUL */

/* The context is public because it has to be: the core never allocates, so a
 * caller declares one on the stack or in static storage. Its fields are an
 * implementation detail - only the three calls below may touch them. */
typedef struct mcdf_micro_sha256 {
  uint32_t      state[8];
  uint64_t      total; /* message length in bytes */
  unsigned char block[64];
  size_t        fill;
} mcdf_micro_sha256;

mcdf_micro_status mcdf_micro_sha256_init(mcdf_micro_sha256 *ctx);
mcdf_micro_status mcdf_micro_sha256_update(mcdf_micro_sha256 *ctx,
                                           const void *data, size_t len);

/* `out` receives MCDF_MICRO_SHA256_SIZE bytes. The context is finished after
 * this and must be re-initialised before reuse. */
mcdf_micro_status mcdf_micro_sha256_final(mcdf_micro_sha256 *ctx,
                                          unsigned char *out);

/* --------------------------------------------------------------- members */

/* Lowercase hex digest of one member's stored bytes, streamed through a small
 * window so nothing scales with member size. `dst` needs
 * MCDF_MICRO_SHA256_HEX_SIZE bytes; a smaller buffer is E_RANGE.
 *
 * The bytes hashed are the ones in the container, which for a sealed member is
 * the ciphertext - that is what makes Integrity hold over an encrypted
 * document and tampering detectable before any key is involved (spec 5.2). */
mcdf_micro_status mcdf_micro_hash_member(mcdf_micro_reader *reader,
                                         const char *path, char *dst,
                                         size_t dst_len);

/* -------------------------------------------------------------- manifest */

/* The Integrity profile: every file manifest.json lists is present and hashes
 * to the recorded digest, and every member the manifest is required to list
 * appears in it (conformance/errors.md, "Manifest membership").
 *
 * Raises E_MISSING_MANIFEST, E_ALGO_NOT_ALLOWED, E_MANIFEST_MISSING_FILE,
 * E_MANIFEST_HASH_MISMATCH and E_MANIFEST_EXTRA_FILE through `on_issue`, which
 * may be NULL. Like mcdf_micro_validate_core(), the return value says whether
 * the check *ran*; `*out_issues` (which may be NULL) says what it found. */
mcdf_micro_status mcdf_micro_verify_manifest(mcdf_micro_reader *reader,
                                             mcdf_micro_issue_fn on_issue,
                                             void *ctx, size_t *out_issues);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MCDF_MICRO_VERIFY_H */
