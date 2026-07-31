/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

#include "jcs.h"

#include <string.h>

#include "mcdf_micro/mcdf_micro_verify.h"

/* RFC 8785 sorts object keys by UTF-16 code unit, not by UTF-8 byte. The two
 * orders agree everywhere except one place: a supplementary character encodes
 * as a surrogate pair starting at 0xD800, so it sorts *below* U+E000..U+FFFF
 * in UTF-16 and *above* them in UTF-8. Member paths are ASCII in practice and
 * this would never fire - which is exactly why it is written out rather than
 * assumed, since the day it fires is the day two implementations disagree
 * about a signed document's bytes. */

struct mm_u16 {
  const unsigned char *p;
  size_t               len;
  size_t               at;
  uint32_t             pending; /* the low surrogate owed from the last step */
  int                  has_pending;
};

static void u16_start(struct mm_u16 *it, const char *s) {
  it->p = (const unsigned char *)s;
  it->len = strlen(s);
  it->at = 0;
  it->pending = 0;
  it->has_pending = 0;
}

/* Next UTF-16 code unit, or -1 at the end of the string. Malformed UTF-8 is
 * consumed byte by byte rather than rejected: this decides an ordering, not a
 * validity, and an ordering has to be total. */
static long u16_next(struct mm_u16 *it) {
  uint32_t cp;
  size_t need, k;

  if (it->has_pending) {
    it->has_pending = 0;
    return (long)it->pending;
  }
  if (it->at >= it->len) return -1;

  cp = it->p[it->at];
  if (cp >= 0xF0u)      { cp &= 0x07u; need = 3; }
  else if (cp >= 0xE0u) { cp &= 0x0Fu; need = 2; }
  else if (cp >= 0xC0u) { cp &= 0x1Fu; need = 1; }
  else                  { need = 0; }

  if (it->at + need >= it->len) need = 0; /* truncated: take the lead byte */
  for (k = 1; k <= need; ++k) {
    const unsigned char c = it->p[it->at + k];
    if ((c & 0xC0u) != 0x80u) { need = k - 1; break; }
    cp = (cp << 6) | (uint32_t)(c & 0x3Fu);
  }
  it->at += need + 1u;

  if (cp <= 0xFFFFu) return (long)cp;
  cp -= 0x10000u;
  it->pending = 0xDC00u + (cp & 0x3FFu);
  it->has_pending = 1;
  return (long)(0xD800u + (cp >> 10));
}

static int jcs_key_less(const char *a, const char *b) {
  struct mm_u16 ia, ib;
  u16_start(&ia, a);
  u16_start(&ib, b);
  for (;;) {
    const long ca = u16_next(&ia);
    const long cb = u16_next(&ib);
    if (ca != cb) return ca < cb;
    if (ca < 0) return 0; /* equal */
  }
}

/* JSON string escaping, RFC 8785 section 3.2.2.2: the seven two-character
 * escapes, \u00XX for the remaining control characters, everything else
 * literal UTF-8. */
static void jcs_string(FILE *out, const char *s) {
  static const char kHex[] = "0123456789abcdef";
  size_t i;

  fputc('"', out);
  for (i = 0; s[i] != '\0'; ++i) {
    const unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"':  fputs("\\\"", out); break;
      case '\\': fputs("\\\\", out); break;
      case '\b': fputs("\\b", out);  break;
      case '\t': fputs("\\t", out);  break;
      case '\n': fputs("\\n", out);  break;
      case '\f': fputs("\\f", out);  break;
      case '\r': fputs("\\r", out);  break;
      default:
        if (c < 0x20u) {
          fputs("\\u00", out);
          fputc(kHex[(c >> 4) & 0x0Fu], out);
          fputc(kHex[c & 0x0Fu], out);
        } else {
          fputc((int)c, out);
        }
    }
  }
  fputc('"', out);
}

mcdf_micro_status mcdf_micro_jcs_manifest(mcdf_micro_reader *reader,
                                          FILE *out) {
  /* Indices into the reader's own index, sorted; the paths stay in the arena
   * and are never copied. */
  static size_t order[MCDF_MICRO_JCS_MAX_FILES];
  size_t n = 0, count, i, j;

  if (reader == NULL || out == NULL) return MCDF_MICRO_E_INVAL;
  if ((mcdf_micro_features() & MCDF_MICRO_FEATURE_INTEGRITY) == 0) {
    return MCDF_MICRO_E_DISABLED;
  }

  count = mcdf_micro_count(reader);
  for (i = 0; i < count; ++i) {
    mcdf_micro_member_info info;
    const mcdf_micro_status st = mcdf_micro_at(reader, i, &info);
    if (st != MCDF_MICRO_OK) return st;
    if (mcdf_micro_manifest_excluded(info.path)) continue;
    if (n == MCDF_MICRO_JCS_MAX_FILES) return MCDF_MICRO_E_ARENA;
    order[n++] = i;
  }

  /* Insertion sort: n is small, the comparator is the expensive part, and a
   * stable in-place sort with no allocation and no qsort callback keeps this
   * file as boring as it deserves to be. */
  for (i = 1; i < n; ++i) {
    const size_t key = order[i];
    mcdf_micro_member_info a;
    if (mcdf_micro_at(reader, key, &a) != MCDF_MICRO_OK) {
      return MCDF_MICRO_E_INVAL;
    }
    j = i;
    while (j > 0) {
      mcdf_micro_member_info b;
      if (mcdf_micro_at(reader, order[j - 1], &b) != MCDF_MICRO_OK) {
        return MCDF_MICRO_E_INVAL;
      }
      if (!jcs_key_less(a.path, b.path)) break;
      order[j] = order[j - 1];
      --j;
    }
    order[j] = key;
  }

  /* The top-level keys are emitted in their sorted order - files <
   * hash_algorithm < mcdf_version - which is fixed because the shape is
   * fixed. `mcdf_version` is this reader's target version, not whatever the
   * stored manifest happens to claim, exactly as the reference builds it. */
  fputs("{\"files\":{", out);
  for (i = 0; i < n; ++i) {
    mcdf_micro_member_info info;
    char hash[MCDF_MICRO_SHA256_HEX_SIZE];
    mcdf_micro_status st = mcdf_micro_at(reader, order[i], &info);
    if (st != MCDF_MICRO_OK) return st;
    st = mcdf_micro_hash_member(reader, info.path, hash, sizeof hash);
    if (st != MCDF_MICRO_OK) return st;

    if (i > 0) fputc(',', out);
    jcs_string(out, info.path);
    fputc(':', out);
    jcs_string(out, hash);
  }
  fputs("},\"hash_algorithm\":\"sha256\",\"mcdf_version\":\"", out);
  fputs(MCDF_MICRO_MCDF_VERSION, out);
  fputs("\"}", out);
  return MCDF_MICRO_OK;
}
