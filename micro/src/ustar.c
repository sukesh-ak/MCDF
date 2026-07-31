/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* USTAR header decoding. Deliberately the whole of the format knowledge: the
 * walk in index.c only sees decoded headers, so the two failure modes that
 * matter on a part with no MMU - an over-read and a bogus length - are
 * contained in one file the fuzzer can chew on. */

#include "internal.h"

/* Field offsets within the 512-byte header (POSIX.1-1988 ustar). */
#define MM_OFF_NAME      0u
#define MM_OFF_SIZE    124u
#define MM_OFF_CHKSUM  148u
#define MM_OFF_TYPE    156u
#define MM_OFF_MAGIC   257u
#define MM_OFF_PREFIX  345u

#define MM_LEN_NAME    100u
#define MM_LEN_SIZE     12u
#define MM_LEN_CHKSUM    8u
#define MM_LEN_PREFIX  155u

/* Length of a NUL-padded fixed-width field. */
static size_t mm_field_len(const unsigned char *p, size_t n) {
  size_t i = 0;
  while (i < n && p[i] != '\0') ++i;
  return i;
}

/* Reads a NUL/space-padded octal field.
 *
 * Returns 0 on a field this reader will not act on: a non-octal byte, or a
 * value past 2^60. The cap is far above any real member (USTAR itself tops out
 * at 8 GiB in 12 octal digits) and exists so callers can add block padding to
 * the result without arithmetic overflow. GNU base-256 extensions are not
 * accepted - the reference packer never emits them, and quietly guessing at a
 * size field is exactly the over-read this library exists to avoid. */
static int mm_octal(const unsigned char *p, size_t n, uint64_t *out) {
  uint64_t value = 0;
  size_t i = 0;
  int digits = 0;

  while (i < n && (p[i] == ' ' || p[i] == '\0')) ++i;
  for (; i < n; ++i) {
    if (p[i] == ' ' || p[i] == '\0') break;
    if (p[i] < '0' || p[i] > '7') return 0;
    if (value > (uint64_t)1 << 60) return 0;
    value = (value << 3) + (uint64_t)(p[i] - '0');
    ++digits;
  }
  /* Trailing bytes after the terminator must be padding, not more digits. */
  for (; i < n; ++i) {
    if (p[i] != ' ' && p[i] != '\0') return 0;
  }
  if (digits == 0) return 0;
  *out = value;
  return 1;
}

/* Unsigned-sum checksum with the checksum field read as spaces.
 *
 * Only the unsigned interpretation is accepted, matching the reference reader
 * in src/container/tar.cpp. Historic archivers also wrote a signed sum, which
 * differs only when a header carries bytes past 0x7F - a non-ASCII member
 * path. Accepting both would let this reader open containers libmcdf rejects,
 * and two implementations disagreeing about which bytes are a document is the
 * one thing an independent implementation must not do. */
static uint32_t mm_checksum(const unsigned char *block) {
  uint32_t sum = 0;
  size_t i;
  for (i = 0; i < MM_BLOCK; ++i) {
    if (i >= MM_OFF_CHKSUM && i < MM_OFF_CHKSUM + MM_LEN_CHKSUM) {
      sum += (uint32_t)' ';
    } else {
      sum += (uint32_t)block[i];
    }
  }
  return sum;
}

static int mm_is_zero_block(const unsigned char *block) {
  size_t i;
  for (i = 0; i < MM_BLOCK; ++i) {
    if (block[i] != 0) return 0;
  }
  return 1;
}

uint64_t mm_padded(uint64_t size) {
  return ((size + MM_BLOCK - 1) / MM_BLOCK) * MM_BLOCK;
}

mcdf_micro_status mm_header_parse(const unsigned char *block,
                                  struct mm_header *out) {
  uint64_t stored = 0;
  size_t name_len, prefix_len, n = 0;

  if (block == NULL || out == NULL) return MCDF_MICRO_E_INVAL;

  out->name[0] = '\0';
  out->name_len = 0;
  out->size = 0;
  out->typeflag = '\0';
  out->is_zero = 0;

  if (mm_is_zero_block(block)) {
    out->is_zero = 1;
    return MCDF_MICRO_OK;
  }

  if (!mm_octal(block + MM_OFF_CHKSUM, MM_LEN_CHKSUM, &stored)) {
    return MCDF_MICRO_E_PARSE;
  }
  if ((uint32_t)stored != mm_checksum(block)) return MCDF_MICRO_E_PARSE;

  if (!mm_octal(block + MM_OFF_SIZE, MM_LEN_SIZE, &out->size)) {
    return MCDF_MICRO_E_PARSE;
  }

  /* The magic is checked but its absence is not fatal: v7 archives leave it
   * empty and carry no prefix field, and one of those is still a readable
   * container. A non-empty magic that is not "ustar" is another format
   * wearing a tar header, and gets refused. */
  if (block[MM_OFF_MAGIC] != '\0') {
    static const char kMagic[5] = {'u', 's', 't', 'a', 'r'};
    size_t i;
    for (i = 0; i < sizeof kMagic; ++i) {
      if (block[MM_OFF_MAGIC + i] != (unsigned char)kMagic[i]) {
        return MCDF_MICRO_E_PARSE;
      }
    }
  }

  name_len = mm_field_len(block + MM_OFF_NAME, MM_LEN_NAME);
  prefix_len = (block[MM_OFF_MAGIC] == '\0')
                   ? 0u
                   : mm_field_len(block + MM_OFF_PREFIX, MM_LEN_PREFIX);
  if (name_len == 0) return MCDF_MICRO_E_PARSE;

  if (prefix_len > 0) {
    size_t i;
    for (i = 0; i < prefix_len; ++i) out->name[n++] = (char)block[MM_OFF_PREFIX + i];
    out->name[n++] = '/';
  }
  {
    size_t i;
    for (i = 0; i < name_len; ++i) out->name[n++] = (char)block[MM_OFF_NAME + i];
  }
  out->name[n] = '\0';   /* n <= 155 + 1 + 100 = 256, one below the array */
  out->name_len = n;
  out->typeflag = (char)block[MM_OFF_TYPE];
  return MCDF_MICRO_OK;
}

/* The name buffer must hold the widest assembly the fields allow. */
MM_STATIC_ASSERT(sizeof(((struct mm_header *)0)->name) >=
                     MM_LEN_PREFIX + 1u + MM_LEN_NAME + 1u,
                 header_name_fits);
