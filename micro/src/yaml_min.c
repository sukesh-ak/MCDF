/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* metadata.yaml, read as the small fixed shape the spec gives it rather than
 * as YAML. Two constructs are understood and no more:
 *
 *   title: Minimal Document          top-level scalar, optionally quoted
 *   authors:                         block sequence whose items carry a name
 *     - name: "The MCDF Project"
 *       id: "did:key:..."
 *
 * A reader needs a title and an author list for a library screen; it does not
 * need anchors, flow mappings, multi-line scalars or tag resolution, and
 * pretending otherwise would put a real YAML parser on a part that cannot
 * afford one. Anything not understood is skipped, never guessed at.
 *
 * Lines longer than MCDF_MICRO_LINE_MAX are reported E_RANGE rather than
 * silently truncated, so a value is either right or refused. */

#include "internal.h"

static int mm_is_space(char c) { return c == ' ' || c == '\t'; }

/* Reads one line into `buf` (NUL-terminated), stripping a trailing CR.
 * Returns E_NOT_FOUND at end of member. `*truncated` is set when the line did
 * not fit; the rest of that line is consumed either way. */
static mcdf_micro_status mm_line(struct mm_cursor *c, char *buf, size_t cap,
                                 size_t *len, int *truncated) {
  size_t n = 0;
  int any = 0;

  *truncated = 0;
  for (;;) {
    unsigned char ch;
    const mcdf_micro_status st = mm_cursor_next(c, &ch);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    any = 1;
    if (ch == '\n') break;
    if (n + 1 < cap) {
      buf[n++] = (char)ch;
    } else {
      *truncated = 1;
    }
  }
  if (!any && n == 0) return MCDF_MICRO_E_NOT_FOUND;
  if (n > 0 && buf[n - 1] == '\r') --n;
  buf[n] = '\0';
  *len = n;
  return MCDF_MICRO_OK;
}

/* Trims spaces and tabs from both ends, in place. */
static void mm_trim(char **s, size_t *len) {
  char *p = *s;
  size_t n = *len;
  while (n > 0 && mm_is_space(p[0])) { ++p; --n; }
  while (n > 0 && mm_is_space(p[n - 1])) --n;
  *s = p;
  *len = n;
}

/* Resolves a YAML scalar in place: strips one layer of quotes and the escapes
 * that can appear inside them, or cuts an unquoted value at a trailing
 * comment. Returns the resolved length. */
static size_t mm_scalar(char *v, size_t len) {
  size_t i, n = 0;

  if (len >= 2 && v[0] == '"') {
    for (i = 1; i < len; ++i) {
      if (v[i] == '"') break;
      if (v[i] == '\\' && i + 1 < len) {
        ++i;
        switch (v[i]) {
          case 'n': v[n++] = '\n'; break;
          case 't': v[n++] = '\t'; break;
          case 'r': v[n++] = '\r'; break;
          case '0': v[n++] = '\0'; break;
          default:  v[n++] = v[i]; break;  /* \" \\ \/ and anything else */
        }
      } else {
        v[n++] = v[i];
      }
    }
    return n;
  }

  if (len >= 2 && v[0] == '\'') {
    for (i = 1; i < len; ++i) {
      if (v[i] == '\'') {
        if (i + 1 < len && v[i + 1] == '\'') { v[n++] = '\''; ++i; continue; }
        break;
      }
      v[n++] = v[i];
    }
    return n;
  }

  /* Unquoted: " #" starts a comment. A '#' with no space before it is part of
   * the value, which is what makes "{#overview}" survive. */
  for (i = 0; i < len; ++i) {
    if (v[i] == '#' && i > 0 && mm_is_space(v[i - 1])) { len = i; break; }
  }
  while (len > 0 && mm_is_space(v[len - 1])) --len;
  return len;
}

/* Splits "key: value" at the first colon followed by space or end-of-line.
 * Returns 0 if the line is not a mapping entry. */
static int mm_split(char *line, size_t len, char **key, size_t *key_len,
                    char **val, size_t *val_len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (line[i] != ':') continue;
    if (i + 1 < len && !mm_is_space(line[i + 1])) continue;  /* "did:key:..." */
    *key = line;
    *key_len = i;
    *val = line + i + 1;
    *val_len = len - (i + 1);
    mm_trim(key, key_len);
    mm_trim(val, val_len);
    return *key_len > 0;
  }
  return 0;
}

/* One walk of metadata.yaml. Exactly one of `want_key` / `want_index` is
 * active: a NULL key means "find the n-th author's name". */
static mcdf_micro_status mm_meta_walk(mcdf_micro_reader *reader,
                                      const char *want_key, size_t want_index,
                                      char *dst, size_t dst_len,
                                      size_t *out_len) {
  struct mm_cursor c;
  char line[MCDF_MICRO_LINE_MAX];
  mcdf_micro_status st;
  int in_authors = 0;
  size_t seen_authors = 0;

  if (reader == NULL) return MCDF_MICRO_E_INVAL;
  if (out_len != NULL) *out_len = 0;

  st = mm_cursor_open(reader, "metadata.yaml", &c);
  if (st != MCDF_MICRO_OK) return st;

  for (;;) {
    size_t len = 0, indent = 0;
    int truncated = 0;
    char *body, *key, *val;
    size_t body_len, key_len, val_len;
    int fresh_item = 0;

    st = mm_line(&c, line, sizeof line, &len, &truncated);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    if (truncated) return MCDF_MICRO_E_RANGE;

    while (indent < len && mm_is_space(line[indent])) ++indent;
    body = line + indent;
    body_len = len - indent;
    if (body_len == 0 || body[0] == '#') continue;
    if (body_len >= 3 && body[0] == '-' && body[1] == '-' && body[2] == '-') {
      continue;  /* document marker */
    }

    if (indent == 0) {
      in_authors = 0;
      if (!mm_split(body, body_len, &key, &key_len, &val, &val_len)) continue;
      key[key_len] = '\0';

      if (want_key == NULL) {
        /* Only an `authors:` with nothing after it opens a block. A flow
         * value - `authors: []` - is a complete answer: no authors. */
        if (mm_streq(key, "authors") && val_len == 0) in_authors = 1;
        continue;
      }
      if (!mm_streq(key, want_key)) continue;
      return mm_emit(val, mm_scalar(val, val_len), dst, dst_len, out_len);
    }

    if (!in_authors || want_key != NULL) continue;

    if (body[0] == '-') {
      ++seen_authors;
      fresh_item = 1;
      ++body;
      --body_len;
      mm_trim(&body, &body_len);
      if (body_len == 0) continue;
    }
    if (seen_authors == 0) continue;

    if (mm_split(body, body_len, &key, &key_len, &val, &val_len)) {
      key[key_len] = '\0';
      if (!mm_streq(key, "name")) continue;
      if (seen_authors - 1 != want_index) continue;
      return mm_emit(val, mm_scalar(val, val_len), dst, dst_len, out_len);
    }
    /* `- Alice`: a bare scalar item is its own name. */
    if (fresh_item && seen_authors - 1 == want_index) {
      return mm_emit(body, mm_scalar(body, body_len), dst, dst_len, out_len);
    }
  }
  return MCDF_MICRO_E_NOT_FOUND;
}

mcdf_micro_status mcdf_micro_meta(mcdf_micro_reader *reader, const char *key,
                                  char *dst, size_t dst_len, size_t *out_len) {
  if (key == NULL) return MCDF_MICRO_E_INVAL;
  return mm_meta_walk(reader, key, 0, dst, dst_len, out_len);
}

mcdf_micro_status mcdf_micro_meta_author(mcdf_micro_reader *reader, size_t index,
                                         char *dst, size_t dst_len,
                                         size_t *out_len) {
  return mm_meta_walk(reader, NULL, index, dst, dst_len, out_len);
}
