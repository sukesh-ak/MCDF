/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The Integrity profile: recompute every member's digest and compare it to
 * manifest.json.
 *
 * Two directions, both required, because they catch opposite tampering:
 * every file the manifest lists must be present and hash right (a member was
 * swapped), and every member the manifest is required to list must appear in
 * it (a member was added). conformance/errors.md fixes which members are
 * exempt; the rule is published as mcdf_micro_manifest_excluded() so a builder
 * and a checker cannot drift apart.
 *
 * The manifest is re-walked per lookup rather than indexed. That is quadratic
 * in the number of entries and deliberate: an index would need the arena, the
 * arena is the caller's, and a document with hundreds of members is not the
 * one a 2 MB part is carrying. Reading is cheap; the hashing dominates. */

#include "mcdf_micro/mcdf_micro_verify.h"

#include "internal.h"

#ifdef MCDF_MICRO_ENABLE_INTEGRITY

/* The only algorithm the spec's allow-list contains (spec 5.1). */
static const char kSha256[] = "sha256";

static void mm_report(mcdf_micro_issue_fn on_issue, void *ctx,
                      mcdf_micro_status code, const char *detail,
                      size_t *issues) {
  if (issues != NULL) *issues += 1u;
  if (on_issue != NULL) on_issue(ctx, code, detail);
}

mcdf_micro_status mcdf_micro_hash_member(mcdf_micro_reader *reader,
                                         const char *path, char *dst,
                                         size_t dst_len) {
  static const char kHex[] = "0123456789abcdef";
  mcdf_micro_member_info info;
  mcdf_micro_sha256 sha;
  unsigned char digest[MCDF_MICRO_SHA256_SIZE];
  unsigned char window[128];
  uint64_t off = 0;
  size_t i;
  mcdf_micro_status st;

  if (dst == NULL) return MCDF_MICRO_E_INVAL;
  if (dst_len < MCDF_MICRO_SHA256_HEX_SIZE) return MCDF_MICRO_E_RANGE;

  st = mcdf_micro_member(reader, path, &info);
  if (st != MCDF_MICRO_OK) return st;

  st = mcdf_micro_sha256_init(&sha);
  if (st != MCDF_MICRO_OK) return st;

  while (off < info.size) {
    const uint64_t remain = info.size - off;
    const size_t want =
        remain < (uint64_t)sizeof window ? (size_t)remain : sizeof window;
    st = mcdf_micro_read_at(reader, path, off, window, want);
    if (st != MCDF_MICRO_OK) return st;
    st = mcdf_micro_sha256_update(&sha, window, want);
    if (st != MCDF_MICRO_OK) return st;
    off += (uint64_t)want;
  }

  st = mcdf_micro_sha256_final(&sha, digest);
  if (st != MCDF_MICRO_OK) return st;

  for (i = 0; i < sizeof digest; ++i) {
    dst[i * 2u]      = kHex[(digest[i] >> 4) & 0x0Fu];
    dst[i * 2u + 1u] = kHex[digest[i] & 0x0Fu];
  }
  dst[sizeof digest * 2u] = '\0';
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_verify_manifest(mcdf_micro_reader *reader,
                                             mcdf_micro_issue_fn on_issue,
                                             void *ctx, size_t *out_issues) {
  char algorithm[MCDF_MICRO_VALUE_MAX];
  size_t listed = 0, i, count;
  mcdf_micro_status st;

  if (reader == NULL) return MCDF_MICRO_E_INVAL;
  if (out_issues != NULL) *out_issues = 0;

  {
    mcdf_micro_member_info info;
    if (mcdf_micro_member(reader, "manifest.json", &info) != MCDF_MICRO_OK) {
      mm_report(on_issue, ctx, MCDF_MICRO_E_MISSING_MANIFEST, "manifest.json",
                out_issues);
      return MCDF_MICRO_OK;
    }
  }

  st = mcdf_micro_manifest_field(reader, "hash_algorithm", algorithm,
                                 sizeof algorithm, NULL);
  if (st != MCDF_MICRO_OK || !mm_streq(algorithm, kSha256)) {
    /* Nothing below this line can mean anything if the digests were not
     * produced by an algorithm we compute, so this stops rather than
     * reporting every file as a mismatch. */
    mm_report(on_issue, ctx, MCDF_MICRO_E_ALGO_NOT_ALLOWED,
              st == MCDF_MICRO_OK ? algorithm : "hash_algorithm is unreadable",
              out_issues);
    return MCDF_MICRO_OK;
  }

  st = mcdf_micro_manifest_count(reader, &listed);
  if (st != MCDF_MICRO_OK) return st;

  for (i = 0; i < listed; ++i) {
    char path[MCDF_MICRO_LINE_MAX];
    char recorded[MCDF_MICRO_VALUE_MAX];
    char actual[MCDF_MICRO_SHA256_HEX_SIZE];

    st = mcdf_micro_manifest_at(reader, i, path, sizeof path, NULL, recorded,
                                sizeof recorded, NULL);
    if (st != MCDF_MICRO_OK) return st;

    st = mcdf_micro_hash_member(reader, path, actual, sizeof actual);
    if (st == MCDF_MICRO_E_NOT_FOUND) {
      mm_report(on_issue, ctx, MCDF_MICRO_E_MANIFEST_MISSING_FILE, path,
                out_issues);
      continue;
    }
    if (st != MCDF_MICRO_OK) return st;
    if (!mm_streq(actual, recorded)) {
      mm_report(on_issue, ctx, MCDF_MICRO_E_MANIFEST_HASH_MISMATCH, path,
                out_issues);
    }
  }

  count = mcdf_micro_count(reader);
  for (i = 0; i < count; ++i) {
    mcdf_micro_member_info info;
    st = mcdf_micro_at(reader, i, &info);
    if (st != MCDF_MICRO_OK) return st;
    if (mcdf_micro_manifest_excluded(info.path)) continue;

    st = mcdf_micro_manifest_hash(reader, info.path, NULL, 0, NULL);
    if (st == MCDF_MICRO_E_NOT_FOUND) {
      mm_report(on_issue, ctx, MCDF_MICRO_E_MANIFEST_EXTRA_FILE, info.path,
                out_issues);
    } else if (st != MCDF_MICRO_OK) {
      return st;
    }
  }
  return MCDF_MICRO_OK;
}

#else /* the gate removes the code, not just the calls */

mcdf_micro_status mcdf_micro_hash_member(mcdf_micro_reader *reader,
                                         const char *path, char *dst,
                                         size_t dst_len) {
  (void)reader; (void)path; (void)dst; (void)dst_len;
  return MCDF_MICRO_E_DISABLED;
}

mcdf_micro_status mcdf_micro_verify_manifest(mcdf_micro_reader *reader,
                                             mcdf_micro_issue_fn on_issue,
                                             void *ctx, size_t *out_issues) {
  (void)reader; (void)on_issue; (void)ctx;
  if (out_issues != NULL) *out_issues = 0;
  return MCDF_MICRO_E_DISABLED;
}

#endif /* MCDF_MICRO_ENABLE_INTEGRITY */
