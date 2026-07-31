/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* schema.yaml, read for the only thing a reader can act on: which sections a
 * document declares, and which of them it declares REQUIRED.
 *
 *   document_type: contract
 *   sections:
 *     - id: overview
 *       title: Overview
 *     - id: terms
 *       required: true
 *
 * `title` and `document_type` are skipped, not because they are unimportant
 * but because nothing here decides anything with them - binding (spec 4.2) is
 * decided by `id` and `required` alone, and a reader that parsed more would be
 * carrying weight it never spends.
 *
 * Streamed through the same 64-byte window as every other member, so a walk
 * costs no arena and nothing scales with file size. */

#include "internal.h"

/* YAML 1.1 truthiness, which is what the reference's YAML library resolves.
 * Anything unrecognised is false, matching the spec's default. */
static int mm_yaml_true(const char *v, size_t len) {
  static const char *const kTrue[] = {"true", "yes", "on", "y", "1"};
  size_t k;
  for (k = 0; k < sizeof kTrue / sizeof kTrue[0]; ++k) {
    const char *w = kTrue[k];
    size_t i = 0;
    while (i < len && w[i] != '\0') {
      const char c = (v[i] >= 'A' && v[i] <= 'Z') ? (char)(v[i] + 32) : v[i];
      if (c != w[i]) break;
      ++i;
    }
    if (i == len && w[i] == '\0') return 1;
  }
  return 0;
}

/* One walk of schema.yaml's `sections:` block. Exactly one mode is active:
 * count the sections, or resolve the n-th one. */
static mcdf_micro_status mm_schema_walk(mcdf_micro_reader *reader,
                                        size_t want_index, size_t *count_out,
                                        char *dst, size_t dst_len,
                                        size_t *out_len, int *required) {
  struct mm_cursor c;
  char line[MCDF_MICRO_LINE_MAX];
  char id[MCDF_MICRO_ID_MAX];
  size_t id_len = 0, seen = 0;
  int in_sections = 0, is_required = 0;
  mcdf_micro_status st;

  id[0] = '\0';

  if (reader == NULL) return MCDF_MICRO_E_INVAL;
  if (out_len != NULL) *out_len = 0;

  st = mm_cursor_open(reader, "schema.yaml", &c);
  if (st != MCDF_MICRO_OK) return st;

  for (;;) {
    size_t len = 0, indent = 0;
    int truncated = 0;
    char *body, *key, *val;
    size_t body_len, key_len, val_len;

    st = mm_line(&c, line, sizeof line, &len, &truncated);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    if (truncated) return MCDF_MICRO_E_RANGE;

    while (indent < len && mm_is_space(line[indent])) ++indent;
    body = line + indent;
    body_len = len - indent;
    if (body_len == 0 || body[0] == '#') continue;
    if (body_len >= 3 && body[0] == '-' && body[1] == '-' && body[2] == '-') {
      continue; /* document marker */
    }

    if (indent == 0) {
      if (in_sections) break; /* the block ended at the next top-level key */
      if (!mm_split(body, body_len, &key, &key_len, &val, &val_len)) continue;
      key[key_len] = '\0';
      /* Only a `sections:` with nothing after it opens a block. A flow value -
       * `sections: []` - is a complete answer: no sections. */
      if (mm_streq(key, "sections") && val_len == 0) in_sections = 1;
      continue;
    }
    if (!in_sections) continue;

    if (body[0] == '-') {
      /* A new item closes the previous one, which is the only moment the
       * previous item's `required:` is known to be final. */
      if (seen > 0 && count_out == NULL && seen - 1 == want_index) {
        if (required != NULL) *required = is_required;
        return mm_emit(id, id_len, dst, dst_len, out_len);
      }
      ++seen;
      /* A section with no `id` is still a section - it is counted and it is
       * reported, as the empty id that spec 4.2 binds nothing to. Skipping it
       * would renumber every section after it. */
      id[0] = '\0';
      id_len = 0;
      is_required = 0;
      ++body;
      --body_len;
      mm_trim(&body, &body_len);
      if (body_len == 0) continue;
    }
    if (seen == 0) continue;

    if (!mm_split(body, body_len, &key, &key_len, &val, &val_len)) continue;
    key[key_len] = '\0';
    val_len = mm_scalar(val, val_len);

    if (mm_streq(key, "id")) {
      if (val_len + 1 > sizeof id) return MCDF_MICRO_E_RANGE;
      {
        size_t i;
        for (i = 0; i < val_len; ++i) id[i] = val[i];
      }
      id[val_len] = '\0';
      id_len = val_len;
    } else if (mm_streq(key, "required")) {
      is_required = mm_yaml_true(val, val_len);
    }
  }

  /* The last item has no successor to close it. */
  if (seen > 0 && count_out == NULL && seen - 1 == want_index) {
    if (required != NULL) *required = is_required;
    return mm_emit(id, id_len, dst, dst_len, out_len);
  }
  if (count_out != NULL) {
    *count_out = seen;
    return MCDF_MICRO_OK;
  }
  return MCDF_MICRO_E_NOT_FOUND;
}

mcdf_micro_status mcdf_micro_schema_count(mcdf_micro_reader *reader,
                                          size_t *out) {
  if (out == NULL) return MCDF_MICRO_E_INVAL;
  *out = 0;
  return mm_schema_walk(reader, 0, out, NULL, 0, NULL, NULL);
}

mcdf_micro_status mcdf_micro_schema_at(mcdf_micro_reader *reader, size_t index,
                                       char *dst, size_t dst_len,
                                       size_t *out_len, int *required) {
  if (required != NULL) *required = 0;
  return mm_schema_walk(reader, index, NULL, dst, dst_len, out_len, required);
}
