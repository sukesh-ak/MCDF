/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* Heading anchors in content.md - the `{#id}` that spec 4.2 binds a
 * schema.yaml section to.
 *
 * Three decisions worth not re-deriving:
 *
 * 1. **A line scanner, not a CommonMark parse.** An anchor is a trailing
 *    `{#id}` on a heading line, and an id is [A-Za-z0-9._-]+ - no markdown
 *    syntax can appear inside one, so there is nothing for an inline parser to
 *    do. What a real parser buys is knowing which lines *are* headings, and
 *    that is a small amount of block state: fences, and the setext underline.
 *
 * 2. **It is never routed through md4c, even in a build that has it.** A
 *    Core-only build and an all-on build must consider exactly the same
 *    documents valid. Deriving anchors from the render path would make
 *    MCDF_MICRO_ENABLE_RENDER change what "valid" means, which is the one thing
 *    a feature gate may not do. The build enforces it: see the guard in
 *    ../CMakeLists.txt.
 *
 * 3. **Line length is not a limit here.** metadata.yaml refuses a line past
 *    MCDF_MICRO_LINE_MAX because a truncated value is worse than no value.
 *    A heading is different: the anchor sits at the *end* of the line, so this
 *    scanner keeps a rolling tail rather than a prefix, and a 4 KB heading is
 *    read correctly. Refusing it would fail a document for being verbose. */

#include "internal.h"

/* Enough for `{#id}` plus an ATX closing sequence and trailing spaces. */
#define MM_TAIL (MCDF_MICRO_ID_MAX + 32u)

struct mm_lineinfo {
  char   tail[MM_TAIL]; /* the last MM_TAIL bytes of the line, linearised */
  size_t tail_len;
  char   head[16]; /* the first bytes, for classification: 3 spaces of indent
                      plus 6 hashes is the widest prefix that decides one */
  size_t head_len;
  size_t len;    /* the whole line's length in bytes */
  size_t indent; /* leading whitespace, in bytes - an index into head/tail */
  size_t columns; /* the same run in columns, a tab counting 4. CommonMark
                     measures indented code in columns, so a tab-indented
                     heading is code there; counting bytes here would make it a
                     heading, and this reader would call a document valid that
                     the reference rejects.

                     A flat 4 per tab is not CommonMark's rule - a tab advances
                     to the next multiple of 4, so " \t" is 4 columns where this
                     counts 5 - and it is nonetheless *exact* for every use
                     below, because all three ask only `columns > 3`. Any tab in
                     the leading run reaches column 4 under either rule, and with
                     no tab the two rules agree byte for byte. Anything that ever
                     needs the true column (list-item content indent, blockquote
                     offsets) must switch to `columns += 4 - columns % 4` and
                     bring a test with it. Do not "fix" this one without one. */
  int    blank;
};

/* Reads one line, keeping its head and its rolling tail. Returns E_NOT_FOUND
 * at end of member. */
static mcdf_micro_status mm_scan_line(struct mm_cursor *c,
                                      struct mm_lineinfo *out) {
  unsigned char ring[MM_TAIL];
  size_t ring_at = 0;
  int any = 0;

  out->tail_len = 0;
  out->head_len = 0;
  out->len = 0;
  out->indent = 0;
  out->columns = 0;
  out->blank = 1;

  for (;;) {
    unsigned char ch;
    const mcdf_micro_status st = mm_cursor_next(c, &ch);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    any = 1;
    if (ch == '\n') break;

    if (out->len == out->indent && (ch == ' ' || ch == '\t')) {
      ++out->indent;
      out->columns += (ch == '\t') ? 4u : 1u;
    }
    if (ch != ' ' && ch != '\t' && ch != '\r') out->blank = 0;
    if (out->head_len < sizeof out->head) out->head[out->head_len++] = (char)ch;

    ring[ring_at] = ch;
    ring_at = (ring_at + 1u) % MM_TAIL;
    ++out->len;
  }
  if (!any && out->len == 0) return MCDF_MICRO_E_NOT_FOUND;

  /* Drop a trailing CR before anything else, so a CRLF document scans
   * identically to an LF one - otherwise every line looks longer than it is
   * and no setext underline is ever recognised on Windows-authored content. */
  if (out->len > 0 && ring[(ring_at + MM_TAIL - 1u) % MM_TAIL] == '\r') {
    --out->len;
    ring_at = (ring_at == 0) ? MM_TAIL - 1u : ring_at - 1u;
    if (out->head_len > out->len) out->head_len = out->len;
    if (out->indent > out->len) out->indent = out->len;
  }
  if (out->blank) out->columns = 0; /* whitespace-only: no content to indent */

  /* Linearise the ring: the whole line when it fits, its last MM_TAIL bytes
   * when it does not. */
  if (out->len <= MM_TAIL) {
    size_t i;
    for (i = 0; i < out->len; ++i) out->tail[i] = (char)ring[i];
    out->tail_len = out->len;
  } else {
    size_t i;
    for (i = 0; i < MM_TAIL; ++i) {
      out->tail[i] = (char)ring[(ring_at + i) % MM_TAIL];
    }
    out->tail_len = MM_TAIL;
  }
  return MCDF_MICRO_OK;
}

/* A fence opener/closer: 3+ of ` or ~ after at most 3 spaces. Returns the
 * fence character, or 0. `*run` receives how many of it there were. */
static char mm_fence(const struct mm_lineinfo *l, size_t *run) {
  const char ch = (l->indent < l->head_len) ? l->head[l->indent] : '\0';
  size_t n = 0;

  if (l->columns > 3) return '\0';
  if (ch != '`' && ch != '~') return '\0';
  while (l->indent + n < l->head_len && l->head[l->indent + n] == ch) ++n;
  /* head is only 8 bytes, so a longer run is measured as "at least that". */
  if (n < 3) return '\0';
  *run = n;
  return ch;
}

/* ATX heading level (1-6), or 0. Requires whitespace or end of line after the
 * hashes - `#tag` is a paragraph, not a heading. */
static int mm_atx_level(const struct mm_lineinfo *l) {
  size_t n = 0;
  if (l->columns > 3) return 0;
  while (l->indent + n < l->head_len && l->head[l->indent + n] == '#') ++n;
  if (n == 0 || n > 6) return 0;
  if (l->indent + n < l->len && !mm_is_space(l->head[l->indent + n])) return 0;
  return (int)n;
}

/* A setext underline: a run of = or - alone on the line, indented 0-3. */
static int mm_setext_level(const struct mm_lineinfo *l) {
  char ch;
  size_t i;
  if (l->blank || l->columns > 3 || l->len != l->tail_len) return 0;
  ch = l->tail[l->indent];
  if (ch != '=' && ch != '-') return 0;
  for (i = l->indent; i < l->tail_len; ++i) {
    if (l->tail[i] == ch) continue;
    if (mm_is_space(l->tail[i])) break;
    return 0;
  }
  for (; i < l->tail_len; ++i) {
    if (!mm_is_space(l->tail[i])) return 0;
  }
  return ch == '=' ? 1 : 2;
}

/* Could this line be the text of a setext heading? Only a paragraph line can
 * be, so what has to be excluded is exactly the lines that open some other
 * block - and no more than those. Excluding by first character alone was too
 * blunt: `*Terms* {#terms}` and `` `id` matters {#x} `` are paragraphs, and
 * calling them non-candidates lost real headings, which under spec 4.2 means
 * failing a document another implementation accepts. */
static int mm_setext_candidate(const struct mm_lineinfo *l) {
  size_t i;
  char ch;

  if (l->blank || l->columns > 3 || l->indent >= l->head_len) return 0;
  ch = l->head[l->indent];

  if (ch == '>') return 0; /* block quote */
  /* A bullet needs whitespace (or end of line) after it; `*emphasis*` and
   * `-dash` are text. */
  if (ch == '-' || ch == '+' || ch == '*') {
    const size_t next = l->indent + 1u;
    if (next >= l->len) return 0;
    if (next < l->head_len && mm_is_space(l->head[next])) return 0;
    return 1;
  }
  /* An ordered-list marker: digits then '.' or ')' then whitespace. */
  if (ch >= '0' && ch <= '9') {
    for (i = l->indent; i < l->head_len && l->head[i] >= '0' && l->head[i] <= '9';
         ++i) {
      /* scan the run */
    }
    if (i < l->head_len && (l->head[i] == '.' || l->head[i] == ')')) {
      const size_t next = i + 1u;
      if (next >= l->len) return 0;
      if (next < l->head_len && mm_is_space(l->head[next])) return 0;
    }
    return 1;
  }
  /* A fence needs three of them; one or two backticks open a code span, which
   * is ordinary text. mm_fence() has already consumed real fences by the time
   * this runs, so this only has to avoid claiming one. */
  if (ch == '`' || ch == '~') {
    size_t run = 0;
    while (l->indent + run < l->head_len && l->head[l->indent + run] == ch) ++run;
    return run < 3;
  }
  /* '#' is an ATX heading only when whitespace follows; `#tag` is text. */
  if (ch == '#') {
    size_t run = 0;
    while (l->indent + run < l->head_len && l->head[l->indent + run] == '#') ++run;
    if (run > 6) return 1; /* seven hashes is not a heading, it is a paragraph */
    if (l->indent + run >= l->len) return 0;
    if (l->indent + run < l->head_len && mm_is_space(l->head[l->indent + run])) {
      return 0;
    }
    return 1;
  }
  return 1;
}

/* Extracts the trailing `{#id}` from a heading's text. Returns the id length,
 * or 0 if there is no anchor. `*out` points into `text`. */
static size_t mm_anchor_of(const char *text, size_t len, const char **out) {
  size_t open;

  while (len > 0 && mm_is_space(text[len - 1])) --len;
  /* An ATX closing sequence (`## Terms {#terms} ##`) is not part of the text;
   * a CommonMark parser removes it before any anchor is visible. */
  {
    size_t hashes = len;
    while (hashes > 0 && text[hashes - 1] == '#') --hashes;
    if (hashes < len && (hashes == 0 || mm_is_space(text[hashes - 1]))) {
      len = hashes;
      while (len > 0 && mm_is_space(text[len - 1])) --len;
    }
  }
  if (len < 4 || text[len - 1] != '}') return 0;

  /* Scan back over the id to its `{#`. */
  open = len - 1;
  while (open > 0) {
    const char c = text[open - 1];
    if (c == '{') return 0; /* `{}` or `{...` without the `#` */
    if (c == '#') break;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
      return 0;
    }
    --open;
  }
  if (open < 2 || text[open - 1] != '#' || text[open - 2] != '{') return 0;
  if (open > len - 1) return 0; /* `{#}` - an empty id binds nothing */
  *out = text + open;
  return len - 1 - open;
}

/* One walk of content.md. Exactly one mode is active: count anchors, match one
 * by id, or resolve the n-th. */
static mcdf_micro_status mm_anchor_walk(mcdf_micro_reader *reader,
                                        const char *want_id, size_t want_index,
                                        size_t *count_out, char *dst,
                                        size_t dst_len, size_t *out_len,
                                        int *level_out) {
  struct mm_cursor c;
  struct mm_lineinfo line, prev;
  size_t index = 0, fence_run = 0;
  char fence_ch = '\0';
  int have_prev = 0;
  mcdf_micro_status st;

  if (reader == NULL) return MCDF_MICRO_E_INVAL;
  if (out_len != NULL) *out_len = 0;

  st = mm_cursor_open(reader, "content.md", &c);
  if (st != MCDF_MICRO_OK) return st;

  for (;;) {
    const char *id = NULL;
    size_t id_len = 0;
    int level = 0;

    st = mm_scan_line(&c, &line);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;

    if (fence_ch != '\0') {
      size_t run = 0;
      if (mm_fence(&line, &run) == fence_ch && run >= fence_run) fence_ch = '\0';
      have_prev = 0;
      continue;
    }
    {
      size_t run = 0;
      const char opened = mm_fence(&line, &run);
      if (opened != '\0') {
        fence_ch = opened;
        fence_run = run;
        have_prev = 0;
        continue;
      }
    }

    level = mm_atx_level(&line);
    if (level > 0) {
      id_len = mm_anchor_of(line.tail, line.tail_len, &id);
      have_prev = 0;
    } else {
      const int setext = mm_setext_level(&line);
      if (setext > 0 && have_prev && mm_setext_candidate(&prev)) {
        level = setext;
        id_len = mm_anchor_of(prev.tail, prev.tail_len, &id);
        have_prev = 0;
      } else {
        prev = line;
        have_prev = 1;
        continue;
      }
    }

    if (id_len == 0) continue;
    ++index;

    if (count_out != NULL) continue;
    if (want_id != NULL) {
      if (id_len == mm_strlen(want_id)) {
        size_t i = 0;
        while (i < id_len && id[i] == want_id[i]) ++i;
        if (i == id_len) return MCDF_MICRO_OK;
      }
      continue;
    }
    if (index - 1 == want_index) {
      if (level_out != NULL) *level_out = level;
      return mm_emit(id, id_len, dst, dst_len, out_len);
    }
  }

  if (count_out != NULL) {
    *count_out = index;
    return MCDF_MICRO_OK;
  }
  return MCDF_MICRO_E_NOT_FOUND;
}

mcdf_micro_status mcdf_micro_anchor_count(mcdf_micro_reader *reader,
                                          size_t *out) {
  if (out == NULL) return MCDF_MICRO_E_INVAL;
  *out = 0;
  return mm_anchor_walk(reader, NULL, 0, out, NULL, 0, NULL, NULL);
}

mcdf_micro_status mcdf_micro_anchor_at(mcdf_micro_reader *reader, size_t index,
                                       char *dst, size_t dst_len,
                                       size_t *out_len, int *level) {
  if (level != NULL) *level = 0;
  return mm_anchor_walk(reader, NULL, index, NULL, dst, dst_len, out_len, level);
}

mcdf_micro_status mcdf_micro_has_anchor(mcdf_micro_reader *reader,
                                        const char *id) {
  if (id == NULL) return MCDF_MICRO_E_INVAL;
  if (id[0] == '\0') return MCDF_MICRO_E_NOT_FOUND;
  return mm_anchor_walk(reader, id, 0, NULL, NULL, 0, NULL, NULL);
}
