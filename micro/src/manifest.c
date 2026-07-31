/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* manifest.json, read as a stream. This is the read side only: recomputing
 * the hashes and comparing them is the Integrity profile, and lands behind
 * MCDF_MICRO_ENABLE_INTEGRITY.
 *
 * The manifest a conforming writer produces is canonical JSON (RFC 8785) -
 * no whitespace, keys sorted, control characters escaped as \u00XX. This
 * parser accepts that and the pretty-printed form a human might commit, since
 * a container assembled by hand in a directory is a first-class authoring
 * workflow in the spec. It is not a general JSON parser: it walks the one
 * shape the manifest has, and refuses anything else rather than guessing. */

#include "internal.h"

struct mm_json {
  struct mm_cursor c;
  unsigned char    peek;
  int              has_peek;
};

static mcdf_micro_status jget(struct mm_json *j, unsigned char *ch) {
  if (j->has_peek) {
    *ch = j->peek;
    j->has_peek = 0;
    return MCDF_MICRO_OK;
  }
  return mm_cursor_next(&j->c, ch);
}

static void jput(struct mm_json *j, unsigned char ch) {
  j->peek = ch;
  j->has_peek = 1;
}

/* Next non-whitespace byte. E_NOT_FOUND means end of member. */
static mcdf_micro_status jws(struct mm_json *j, unsigned char *ch) {
  for (;;) {
    const mcdf_micro_status st = jget(j, ch);
    if (st != MCDF_MICRO_OK) return st;
    if (*ch == ' ' || *ch == '\t' || *ch == '\n' || *ch == '\r') continue;
    return MCDF_MICRO_OK;
  }
}

static void jput_byte(char *buf, size_t cap, size_t *n, char ch) {
  if (buf != NULL && *n + 1 < cap) buf[*n] = ch;
  *n += 1u;
}

static int jhex(unsigned char ch, uint32_t *nib) {
  if (ch >= '0' && ch <= '9') { *nib = (uint32_t)(ch - '0'); return 1; }
  if (ch >= 'a' && ch <= 'f') { *nib = (uint32_t)(ch - 'a') + 10u; return 1; }
  if (ch >= 'A' && ch <= 'F') { *nib = (uint32_t)(ch - 'A') + 10u; return 1; }
  return 0;
}

static mcdf_micro_status jhex4(struct mm_json *j, uint32_t *out) {
  uint32_t value = 0;
  int i;
  for (i = 0; i < 4; ++i) {
    unsigned char ch;
    uint32_t nib;
    const mcdf_micro_status st = jget(j, &ch);
    if (st != MCDF_MICRO_OK) return MCDF_MICRO_E_PARSE;
    if (!jhex(ch, &nib)) return MCDF_MICRO_E_PARSE;
    value = (value << 4) | nib;
  }
  *out = value;
  return MCDF_MICRO_OK;
}

static void jput_utf8(char *buf, size_t cap, size_t *n, uint32_t cp) {
  if (cp < 0x80u) {
    jput_byte(buf, cap, n, (char)cp);
  } else if (cp < 0x800u) {
    jput_byte(buf, cap, n, (char)(0xC0u | (cp >> 6)));
    jput_byte(buf, cap, n, (char)(0x80u | (cp & 0x3Fu)));
  } else if (cp < 0x10000u) {
    jput_byte(buf, cap, n, (char)(0xE0u | (cp >> 12)));
    jput_byte(buf, cap, n, (char)(0x80u | ((cp >> 6) & 0x3Fu)));
    jput_byte(buf, cap, n, (char)(0x80u | (cp & 0x3Fu)));
  } else {
    jput_byte(buf, cap, n, (char)(0xF0u | (cp >> 18)));
    jput_byte(buf, cap, n, (char)(0x80u | ((cp >> 12) & 0x3Fu)));
    jput_byte(buf, cap, n, (char)(0x80u | ((cp >> 6) & 0x3Fu)));
    jput_byte(buf, cap, n, (char)(0x80u | (cp & 0x3Fu)));
  }
}

/* Reads a string body, the opening quote already consumed. Decodes into `buf`
 * when it fits and always reports the true decoded length in `*len`, so a
 * caller can tell "too long" from "wrong". `buf` may be NULL to skip. */
static mcdf_micro_status jstring(struct mm_json *j, char *buf, size_t cap,
                                 size_t *len) {
  size_t n = 0;
  for (;;) {
    unsigned char ch;
    mcdf_micro_status st = jget(j, &ch);
    if (st == MCDF_MICRO_E_NOT_FOUND) return MCDF_MICRO_E_PARSE;
    if (st != MCDF_MICRO_OK) return st;

    if (ch == '"') break;
    if (ch != '\\') {
      jput_byte(buf, cap, &n, (char)ch);
      continue;
    }

    st = jget(j, &ch);
    if (st != MCDF_MICRO_OK) return MCDF_MICRO_E_PARSE;
    switch (ch) {
      case '"':  jput_byte(buf, cap, &n, '"');  break;
      case '\\': jput_byte(buf, cap, &n, '\\'); break;
      case '/':  jput_byte(buf, cap, &n, '/');  break;
      case 'b':  jput_byte(buf, cap, &n, '\b'); break;
      case 'f':  jput_byte(buf, cap, &n, '\f'); break;
      case 'n':  jput_byte(buf, cap, &n, '\n'); break;
      case 'r':  jput_byte(buf, cap, &n, '\r'); break;
      case 't':  jput_byte(buf, cap, &n, '\t'); break;
      case 'u': {
        uint32_t cp = 0;
        st = jhex4(j, &cp);
        if (st != MCDF_MICRO_OK) return st;
        /* A high surrogate is only meaningful paired; an unpaired one is left
         * as U+FFFD rather than emitted as invalid UTF-8. */
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
          unsigned char a, b;
          uint32_t lo = 0;
          if (jget(j, &a) == MCDF_MICRO_OK && a == '\\' &&
              jget(j, &b) == MCDF_MICRO_OK && b == 'u' &&
              jhex4(j, &lo) == MCDF_MICRO_OK && lo >= 0xDC00u && lo <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
          } else {
            return MCDF_MICRO_E_PARSE;
          }
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
          return MCDF_MICRO_E_PARSE;
        }
        jput_utf8(buf, cap, &n, cp);
        break;
      }
      default:
        return MCDF_MICRO_E_PARSE;
    }
  }
  if (buf != NULL && cap > 0) buf[n < cap ? n : cap - 1] = '\0';
  if (len != NULL) *len = n;
  return MCDF_MICRO_OK;
}

/* Consumes exactly one value and discards it, including nested containers. */
static mcdf_micro_status jskip(struct mm_json *j) {
  int depth = 0;
  for (;;) {
    unsigned char ch;
    mcdf_micro_status st = jws(j, &ch);
    if (st != MCDF_MICRO_OK) return MCDF_MICRO_E_PARSE;

    if (ch == '"') {
      st = jstring(j, NULL, 0, NULL);
      if (st != MCDF_MICRO_OK) return st;
    } else if (ch == '{' || ch == '[') {
      ++depth;
      continue;
    } else if (ch == '}' || ch == ']') {
      if (depth == 0) { jput(j, ch); return MCDF_MICRO_OK; }
      --depth;
    } else if (ch == ',' || ch == ':') {
      if (ch == ',' && depth == 0) { jput(j, ch); return MCDF_MICRO_OK; }
      continue;
    } else {
      /* A bare literal: number, true, false or null. */
      for (;;) {
        st = jget(j, &ch);
        if (st == MCDF_MICRO_E_NOT_FOUND) break;
        if (st != MCDF_MICRO_OK) return st;
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' ||
            ch == '\n' || ch == '\r') {
          jput(j, ch);
          break;
        }
      }
    }
    if (depth == 0) return MCDF_MICRO_OK;
  }
}

/* Opens manifest.json and consumes the '{' that starts it. */
static mcdf_micro_status jopen(mcdf_micro_reader *r, struct mm_json *j) {
  unsigned char ch;
  mcdf_micro_status st;

  if (r == NULL) return MCDF_MICRO_E_INVAL;
  j->has_peek = 0;
  j->peek = 0;
  st = mm_cursor_open(r, "manifest.json", &j->c);
  if (st != MCDF_MICRO_OK) return st;
  st = jws(j, &ch);
  if (st != MCDF_MICRO_OK || ch != '{') return MCDF_MICRO_E_PARSE;
  return MCDF_MICRO_OK;
}

/* Reads the next `"key":` inside an object.
 * Returns E_NOT_FOUND when the object ends. */
static mcdf_micro_status jkey(struct mm_json *j, int first, char *buf,
                              size_t cap, size_t *len) {
  unsigned char ch;
  mcdf_micro_status st = jws(j, &ch);
  if (st != MCDF_MICRO_OK) return MCDF_MICRO_E_PARSE;

  if (ch == '}') return MCDF_MICRO_E_NOT_FOUND;
  if (!first) {
    if (ch != ',') return MCDF_MICRO_E_PARSE;
    st = jws(j, &ch);
    if (st != MCDF_MICRO_OK) return MCDF_MICRO_E_PARSE;
  }
  if (ch != '"') return MCDF_MICRO_E_PARSE;

  st = jstring(j, buf, cap, len);
  if (st != MCDF_MICRO_OK) return st;

  st = jws(j, &ch);
  if (st != MCDF_MICRO_OK || ch != ':') return MCDF_MICRO_E_PARSE;
  return MCDF_MICRO_OK;
}

/* Reads a string value into the caller's buffer, via the bounded staging
 * buffer that caps how long a manifest scalar may be. */
static mcdf_micro_status jvalue_emit(struct mm_json *j, char *dst,
                                     size_t dst_len, size_t *out_len) {
  char staged[MCDF_MICRO_VALUE_MAX];
  unsigned char ch;
  size_t len = 0;
  mcdf_micro_status st = jws(j, &ch);

  if (st != MCDF_MICRO_OK || ch != '"') return MCDF_MICRO_E_PARSE;
  st = jstring(j, staged, sizeof staged, &len);
  if (st != MCDF_MICRO_OK) return st;
  if (out_len != NULL) *out_len = len;
  if (len + 1 > sizeof staged) return MCDF_MICRO_E_RANGE;
  return mm_emit(staged, len, dst, dst_len, out_len);
}

/* ------------------------------------------------------------ public API */

mcdf_micro_status mcdf_micro_manifest_field(mcdf_micro_reader *reader,
                                            const char *key, char *dst,
                                            size_t dst_len, size_t *out_len) {
  struct mm_json j;
  char name[MCDF_MICRO_LINE_MAX];
  size_t name_len = 0;
  int first = 1;
  mcdf_micro_status st;

  if (key == NULL) return MCDF_MICRO_E_INVAL;
  if (out_len != NULL) *out_len = 0;
  st = jopen(reader, &j);
  if (st != MCDF_MICRO_OK) return st;

  for (;;) {
    st = jkey(&j, first, name, sizeof name, &name_len);
    if (st == MCDF_MICRO_E_NOT_FOUND) return MCDF_MICRO_E_NOT_FOUND;
    if (st != MCDF_MICRO_OK) return st;
    first = 0;

    if (name_len < sizeof name && mm_streq(name, key)) {
      return jvalue_emit(&j, dst, dst_len, out_len);
    }
    st = jskip(&j);
    if (st != MCDF_MICRO_OK) return st;
  }
}

/* Walks the "files" object. Exactly one mode is active: match a path, take the
 * n-th entry, or count. */
static mcdf_micro_status mm_files_walk(mcdf_micro_reader *reader,
                                       const char *want_path, size_t want_index,
                                       size_t *count_out,
                                       char *path_dst, size_t path_dst_len,
                                       size_t *path_out_len,
                                       char *hash_dst, size_t hash_dst_len,
                                       size_t *hash_out_len) {
  struct mm_json j;
  char name[MCDF_MICRO_LINE_MAX];
  size_t name_len = 0, index = 0;
  int first = 1, found_files = 0;
  mcdf_micro_status st = jopen(reader, &j);

  if (st != MCDF_MICRO_OK) return st;

  /* Find "files". */
  for (;;) {
    st = jkey(&j, first, name, sizeof name, &name_len);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    first = 0;
    if (name_len < sizeof name && mm_streq(name, "files")) {
      found_files = 1;
      break;
    }
    st = jskip(&j);
    if (st != MCDF_MICRO_OK) return st;
  }
  if (!found_files) return MCDF_MICRO_E_NOT_FOUND;

  {
    unsigned char ch;
    st = jws(&j, &ch);
    if (st != MCDF_MICRO_OK || ch != '{') return MCDF_MICRO_E_PARSE;
  }

  first = 1;
  for (;;) {
    st = jkey(&j, first, name, sizeof name, &name_len);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    first = 0;

    if (count_out == NULL) {
      const int hit = (want_path != NULL)
                          ? (name_len < sizeof name && mm_streq(name, want_path))
                          : (index == want_index);
      if (hit) {
        if (want_path == NULL) {
          if (name_len + 1 > sizeof name) return MCDF_MICRO_E_RANGE;
          st = mm_emit(name, name_len, path_dst, path_dst_len, path_out_len);
          if (st != MCDF_MICRO_OK) return st;
        }
        return jvalue_emit(&j, hash_dst, hash_dst_len, hash_out_len);
      }
    }
    ++index;
    st = jskip(&j);
    if (st != MCDF_MICRO_OK) return st;
  }

  if (count_out != NULL) {
    *count_out = index;
    return MCDF_MICRO_OK;
  }
  return MCDF_MICRO_E_NOT_FOUND;
}

mcdf_micro_status mcdf_micro_manifest_hash(mcdf_micro_reader *reader,
                                           const char *path, char *dst,
                                           size_t dst_len, size_t *out_len) {
  if (path == NULL) return MCDF_MICRO_E_INVAL;
  if (out_len != NULL) *out_len = 0;
  return mm_files_walk(reader, path, 0, NULL, NULL, 0, NULL, dst, dst_len,
                       out_len);
}

mcdf_micro_status mcdf_micro_manifest_count(mcdf_micro_reader *reader,
                                            size_t *out) {
  if (out == NULL) return MCDF_MICRO_E_INVAL;
  *out = 0;
  return mm_files_walk(reader, NULL, 0, out, NULL, 0, NULL, NULL, 0, NULL);
}

mcdf_micro_status mcdf_micro_manifest_at(mcdf_micro_reader *reader, size_t index,
                                         char *path_dst, size_t path_dst_len,
                                         size_t *path_out_len, char *hash_dst,
                                         size_t hash_dst_len,
                                         size_t *hash_out_len) {
  if (path_out_len != NULL) *path_out_len = 0;
  if (hash_out_len != NULL) *hash_out_len = 0;
  return mm_files_walk(reader, NULL, index, NULL, path_dst, path_dst_len,
                       path_out_len, hash_dst, hash_dst_len, hash_out_len);
}
