/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* Fuzzes the mcdf_micro USTAR walker - the surface every untrusted archive
 * hits first - then everything the resulting index reaches.
 *
 * This matters more here than it does for the desktop runtime. libmcdf parses
 * untrusted input on a machine with an MMU, where an over-read is a segfault
 * and a crash report. mcdf_micro parses it on a part with neither, where the
 * same bug is a silent read of whatever the flash controller had mapped next.
 * The walker's two guards - the archive is a whole number of blocks, and a
 * declared member size must fit inside what remains - are what the fuzzer is
 * here to try to get past. */

#include <string.h>

#include "mcdf_micro/mcdf_micro.h"
#include "mcdf_micro/mcdf_micro_verify.h"

#include "driver.h"

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

/* The reader is told nothing about how much it may index: a small arena means
 * most inputs stop at E_ARENA and the interesting paths never run. */
static unsigned char g_arena[MCDF_MICRO_ARENA_SIZE(256, 16384)];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  struct span span;
  mcdf_micro_source src;
  mcdf_micro_reader *reader = NULL;
  char scratch[512];
  size_t count, i, n = 0;

  span.bytes = data;
  span.len = size;
  src.ctx = &span;
  src.read = span_read;
  src.size = (uint64_t)size;

  if (mcdf_micro_open(&src, g_arena, sizeof g_arena, &reader) != MCDF_MICRO_OK) {
    return 0;
  }

  count = mcdf_micro_count(reader);
  for (i = 0; i < count; ++i) {
    mcdf_micro_member_info info;
    if (mcdf_micro_at(reader, i, &info) != MCDF_MICRO_OK) continue;
    (void)mcdf_micro_path_is_safe(info.path);
    (void)mcdf_micro_manifest_excluded(info.path);
    (void)mcdf_micro_member(reader, info.path, &info);
    /* Windows at both ends of the member, plus one deliberately past it. */
    (void)mcdf_micro_read_at(reader, info.path, 0, scratch, sizeof scratch);
    if (info.size > 4) {
      (void)mcdf_micro_read_at(reader, info.path, info.size - 4, scratch, 4);
    }
    (void)mcdf_micro_read_at(reader, info.path, info.size, scratch, 1);
  }

  /* Every member parser, over whatever the archive happens to contain. */
  (void)mcdf_micro_meta(reader, "title", scratch, sizeof scratch, &n);
  (void)mcdf_micro_meta_author(reader, 0, scratch, sizeof scratch, &n);
  (void)mcdf_micro_manifest_field(reader, "hash_algorithm", scratch,
                                  sizeof scratch, &n);
  (void)mcdf_micro_manifest_count(reader, &n);
  (void)mcdf_micro_manifest_at(reader, 0, scratch, sizeof scratch, &n, NULL, 0,
                               NULL);
  (void)mcdf_micro_schema_count(reader, &n);
  (void)mcdf_micro_schema_at(reader, 0, scratch, sizeof scratch, &n, NULL);
  (void)mcdf_micro_anchor_count(reader, &n);
  (void)mcdf_micro_anchor_at(reader, 0, scratch, sizeof scratch, &n, NULL);
  (void)mcdf_micro_is_sealed(reader, "content.md");

  /* The profiles, end to end - which also drags the hasher over every member. */
  (void)mcdf_micro_validate_core(reader, NULL, NULL, &n);
  (void)mcdf_micro_verify_manifest(reader, NULL, NULL, &n);

  mcdf_micro_close(reader);
  return 0;
}
