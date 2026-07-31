/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* Fuzzes the mcdf_micro member parsers directly.
 *
 * fuzz_micro_tar reaches them too, but only through a valid archive - so the
 * fuzzer spends nearly all of its budget learning to satisfy a USTAR checksum
 * and almost none of it inside the JSON and YAML scanners. This harness wraps
 * the raw input as the sole member of a well-formed archive, once per member
 * name, so every mutation lands in a parser instead of in a header.
 *
 * All five shapes are covered in one target because they share the cursor and
 * the line splitter underneath: a bug in that is a bug in all of them, and
 * splitting the harness would just divide the corpus. */

#include <stdio.h>
#include <string.h>

#include "mcdf_micro/mcdf_micro.h"
#include "mcdf_micro/mcdf_micro_verify.h"

#include "driver.h"

#define BLOCK    512u
#define MAX_BODY (256u * 1024u)

static unsigned char g_archive[MAX_BODY + 4u * BLOCK];
static size_t        g_archive_len;
static unsigned char g_arena[MCDF_MICRO_ARENA_SIZE(16, 1024)];

struct span {
  const unsigned char *bytes;
  size_t               len;
};

static int span_read(void *ctx, uint64_t off, void *dst, size_t len) {
  const struct span *s = (const struct span *)ctx;
  if (off > (uint64_t)s->len || (uint64_t)len > (uint64_t)s->len - off) {
    return -1;
  }
  memcpy(dst, s->bytes + (size_t)off, len);
  return 0;
}

static void put_octal(unsigned char *field, size_t width, unsigned long value) {
  size_t i;
  field[width - 1] = '\0';
  for (i = width - 1; i > 0; --i) {
    field[i - 1] = (unsigned char)('0' + (value & 7u));
    value >>= 3;
  }
}

/* One regular member, then the two-block end marker. */
static void build(const char *name, const uint8_t *data, size_t size) {
  unsigned char *h = g_archive;
  unsigned long sum = 0;
  size_t i, blocks;

  memset(g_archive, 0, sizeof g_archive);
  memcpy(h, name, strlen(name));
  put_octal(h + 100, 8, 0644);
  put_octal(h + 108, 8, 0);
  put_octal(h + 116, 8, 0);
  put_octal(h + 124, 12, (unsigned long)size);
  put_octal(h + 136, 12, 0);
  h[156] = '0';
  memcpy(h + 257, "ustar", 5);
  h[263] = '0';
  h[264] = '0';

  memset(h + 148, ' ', 8);
  for (i = 0; i < BLOCK; ++i) sum += h[i];
  snprintf((char *)h + 148, 7, "%06lo", sum);
  h[154] = '\0';
  h[155] = ' ';

  blocks = (size + BLOCK - 1u) / BLOCK;
  memcpy(g_archive + BLOCK, data, size);
  g_archive_len = BLOCK + blocks * BLOCK + 2u * BLOCK;
}

static void exercise(const char *name, const uint8_t *data, size_t size) {
  struct span span;
  mcdf_micro_source src;
  mcdf_micro_reader *reader = NULL;
  char scratch[256];
  size_t n = 0;

  build(name, data, size);
  span.bytes = g_archive;
  span.len = g_archive_len;
  src.ctx = &span;
  src.read = span_read;
  src.size = (uint64_t)g_archive_len;

  if (mcdf_micro_open(&src, g_arena, sizeof g_arena, &reader) != MCDF_MICRO_OK) {
    return;
  }

  (void)mcdf_micro_meta(reader, "title", scratch, sizeof scratch, &n);
  (void)mcdf_micro_meta(reader, "", scratch, sizeof scratch, &n);
  (void)mcdf_micro_meta_author(reader, 0, scratch, sizeof scratch, &n);
  (void)mcdf_micro_meta_author(reader, 3, scratch, sizeof scratch, &n);

  (void)mcdf_micro_manifest_field(reader, "hash_algorithm", scratch,
                                  sizeof scratch, &n);
  (void)mcdf_micro_manifest_hash(reader, "content.md", scratch, sizeof scratch,
                                 &n);
  (void)mcdf_micro_manifest_count(reader, &n);
  (void)mcdf_micro_manifest_at(reader, 0, scratch, sizeof scratch, &n, NULL, 0,
                               NULL);

  (void)mcdf_micro_schema_count(reader, &n);
  (void)mcdf_micro_schema_at(reader, 0, scratch, sizeof scratch, &n, NULL);
  (void)mcdf_micro_schema_at(reader, 7, scratch, sizeof scratch, &n, NULL);

  (void)mcdf_micro_anchor_count(reader, &n);
  (void)mcdf_micro_anchor_at(reader, 0, scratch, sizeof scratch, &n, NULL);
  (void)mcdf_micro_has_anchor(reader, "overview");

  (void)mcdf_micro_is_sealed(reader, "content.md");
  (void)mcdf_micro_validate_core(reader, NULL, NULL, &n);
  (void)mcdf_micro_verify_manifest(reader, NULL, NULL, &n);

  mcdf_micro_close(reader);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static const char *const kNames[] = {"manifest.json", "metadata.yaml",
                                       "schema.yaml", "content.md",
                                       "encryption/policy.yaml"};
  size_t i;

  if (size > MAX_BODY) return 0;
  for (i = 0; i < sizeof kNames / sizeof kNames[0]; ++i) {
    exercise(kNames[i], data, size);
  }
  return 0;
}
