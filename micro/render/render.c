/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* content.md as a block/span event stream, over vendored md4c.
 *
 * The whole of this file is translation: md4c's vocabulary in, mcdf_micro's
 * out. It is deliberately thin, and the three places it is not are the three
 * places a layout engine would otherwise have to reimplement the spec:
 *
 *   - a sealed content.md is refused rather than parsed (spec 6);
 *   - a link or image destination is matched against the container, so the
 *     caller knows whether it may read it as a member (spec 4.1);
 *   - a heading's `{#id}` anchor is stripped from its text and reported as an
 *     id (spec 10.4), rather than leaking into what gets drawn.
 *
 * This is also the only translation unit that includes md4c, which is what the
 * configure-time guard in ../CMakeLists.txt enforces: nothing that decides
 * whether a document is *valid* may reach for a parser that exists only in
 * some build configurations. */

#include "mcdf_micro/mcdf_micro_render.h"

#include "internal.h"

#ifdef MCDF_MICRO_ENABLE_RENDER

#include <limits.h>

#include "md4c.h"

struct mm_render {
  const mcdf_micro_render_callbacks *cb;
  void                              *ctx;
  mcdf_micro_reader                 *reader;

  /* The heading being read, if any. Its text is held back by exactly one
   * chunk so the trailing `{#id}` can be split off the last one - see
   * mm_flush_held(). One chunk of lookahead is all it takes, and it costs no
   * buffer at all: an anchor is plain text, so md4c never splits it. */
  int              in_heading;
  mcdf_micro_slice held;
  int              have_held;
  int              held_type;

  /* Set when a *callback* stopped the parse, as opposed to this file refusing
   * a construct or md4c running out of memory. md_parse() reports all three
   * the same way, and a caller that asked to stop must not be told its
   * document is broken. */
  int aborted;

  /* md4c wraps every parse in a document block, so a windowed walk would emit
   * one per window - and a caller counting them would see several documents
   * where the reader opened one. The document block is opened once, at the
   * first window, and closed once, after the last. */
  int skip_doc_enter;
  int skip_doc_leave;
};

/* Records a callback's verdict on the way out. */
static int mm_cb(struct mm_render *r, int rc) {
  if (rc != 0) r->aborted = 1;
  return rc;
}

/* ---------------------------------------------------------------- helpers */

static mcdf_micro_slice mm_slice(const MD_CHAR *text, MD_SIZE size) {
  mcdf_micro_slice s;
  s.text = text;
  s.len = (size_t)size;
  return s;
}

static mcdf_micro_slice mm_attr(const MD_ATTRIBUTE *a) {
  /* An MD_ATTRIBUTE can be a chain of substrings when it contains entities or
   * escapes. Reporting the raw span is right for the two things callers do
   * with a destination - compare it to a member path, or hand it to a URL
   * opener - and decoding it here would mean allocating. */
  return mm_slice(a->text, a->size);
}

static int mm_emit_text(struct mm_render *r, int type, const char *text,
                        size_t len) {
  if (r->cb->text == NULL || len == 0) return 0;
  return mm_cb(r, r->cb->text(r->ctx, (mcdf_micro_text_type)type, text, len));
}

/* Emits the chunk being held back, if any. */
static int mm_flush_held(struct mm_render *r) {
  int rc = 0;
  if (r->have_held) {
    rc = mm_emit_text(r, r->held_type, r->held.text, r->held.len);
    r->have_held = 0;
  }
  return rc;
}

/* Splits a trailing `{#id}` off the held chunk. Returns the id; the held
 * chunk is shortened to the text that precedes it, with trailing spaces
 * dropped the way a CommonMark renderer drops them. */
static mcdf_micro_slice mm_take_anchor(struct mm_render *r) {
  mcdf_micro_slice id;
  const char *t;
  size_t n, open;

  id.text = NULL;
  id.len = 0;
  if (!r->have_held || r->held_type != MCDF_MICRO_TEXT_NORMAL) return id;

  t = r->held.text;
  n = r->held.len;
  if (n < 4 || t[n - 1] != '}') return id;

  /* Walk back over the id to its `{#`. The charset is spec 4.2's. */
  open = n - 1;
  while (open > 0) {
    const char c = t[open - 1];
    if (c == '#') break;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
      return id;
    }
    --open;
  }
  if (open < 2 || t[open - 1] != '#' || t[open - 2] != '{') return id;
  if (open > n - 1) return id; /* `{#}` - an empty anchor binds nothing */

  id.text = t + open;
  id.len = n - 1 - open;

  r->held.len = open - 2;
  while (r->held.len > 0 && (r->held.text[r->held.len - 1] == ' ' ||
                             r->held.text[r->held.len - 1] == '\t')) {
    --r->held.len;
  }
  return id;
}

/* --------------------------------------------------------- md4c callbacks */

static int mm_enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  struct mm_render *r = (struct mm_render *)userdata;
  mcdf_micro_heading_detail h;
  mcdf_micro_list_detail list;
  mcdf_micro_code_detail code;
  const void *out = NULL;
  mcdf_micro_block block;

  if (mm_flush_held(r) != 0) return 1;

  if (type == MD_BLOCK_DOC && r->skip_doc_enter) return 0;

  switch (type) {
    case MD_BLOCK_DOC:   block = MCDF_MICRO_BLOCK_DOC;   break;
    case MD_BLOCK_QUOTE: block = MCDF_MICRO_BLOCK_QUOTE; break;
    case MD_BLOCK_HR:    block = MCDF_MICRO_BLOCK_HR;    break;
    case MD_BLOCK_P:     block = MCDF_MICRO_BLOCK_P;     break;
    case MD_BLOCK_UL: {
      const MD_BLOCK_UL_DETAIL *d = (const MD_BLOCK_UL_DETAIL *)detail;
      block = MCDF_MICRO_BLOCK_UL;
      list.is_tight = d->is_tight;
      list.start = 0;
      list.mark = d->mark;
      out = &list;
      break;
    }
    case MD_BLOCK_OL: {
      const MD_BLOCK_OL_DETAIL *d = (const MD_BLOCK_OL_DETAIL *)detail;
      block = MCDF_MICRO_BLOCK_OL;
      list.is_tight = d->is_tight;
      list.start = d->start;
      list.mark = d->mark_delimiter;
      out = &list;
      break;
    }
    case MD_BLOCK_LI:
      block = MCDF_MICRO_BLOCK_LI;
      break;
    case MD_BLOCK_H: {
      const MD_BLOCK_H_DETAIL *d = (const MD_BLOCK_H_DETAIL *)detail;
      block = MCDF_MICRO_BLOCK_H;
      h.level = (int)d->level;
      h.id.text = NULL; /* not known until the heading's text has been read */
      h.id.len = 0;
      out = &h;
      r->in_heading = 1;
      break;
    }
    case MD_BLOCK_CODE: {
      const MD_BLOCK_CODE_DETAIL *d = (const MD_BLOCK_CODE_DETAIL *)detail;
      block = MCDF_MICRO_BLOCK_CODE;
      code.info = mm_attr(&d->info);
      code.fence = d->fence_char;
      out = &code;
      break;
    }
    default:
      /* MCDF fixes the dialect, so tables and raw HTML blocks cannot appear
       * (spec 10.4). Reaching here means md4c was configured with extensions
       * this file does not know about, which is a build defect, not a
       * document defect - refuse rather than silently drop content. */
      return 1;
  }

  if (r->cb->enter_block == NULL) return 0;
  return mm_cb(r, r->cb->enter_block(r->ctx, block, out));
}

static int mm_leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  struct mm_render *r = (struct mm_render *)userdata;
  mcdf_micro_heading_detail h;
  const void *out = NULL;
  mcdf_micro_block block;

  if (type == MD_BLOCK_H) {
    /* The anchor is at the end of the heading, so this is the first moment it
     * can be known. Splitting it off the held chunk also keeps it out of the
     * text the caller draws. */
    const MD_BLOCK_H_DETAIL *d = (const MD_BLOCK_H_DETAIL *)detail;
    h.level = (int)d->level;
    h.id = mm_take_anchor(r);
    out = &h;
    r->in_heading = 0;
  }
  if (mm_flush_held(r) != 0) return 1;
  if (type == MD_BLOCK_DOC && r->skip_doc_leave) return 0;

  switch (type) {
    case MD_BLOCK_DOC:   block = MCDF_MICRO_BLOCK_DOC;   break;
    case MD_BLOCK_QUOTE: block = MCDF_MICRO_BLOCK_QUOTE; break;
    case MD_BLOCK_UL:    block = MCDF_MICRO_BLOCK_UL;    break;
    case MD_BLOCK_OL:    block = MCDF_MICRO_BLOCK_OL;    break;
    case MD_BLOCK_LI:    block = MCDF_MICRO_BLOCK_LI;    break;
    case MD_BLOCK_HR:    block = MCDF_MICRO_BLOCK_HR;    break;
    case MD_BLOCK_H:     block = MCDF_MICRO_BLOCK_H;     break;
    case MD_BLOCK_CODE:  block = MCDF_MICRO_BLOCK_CODE;  break;
    case MD_BLOCK_P:     block = MCDF_MICRO_BLOCK_P;     break;
    default:             return 1;
  }

  if (r->cb->leave_block == NULL) return 0;
  return mm_cb(r, r->cb->leave_block(r->ctx, block, out));
}

/* Non-zero when `dest` names a member of this container that is safe to use as
 * a relative path. Deliberately an exact match: a destination carrying a
 * fragment or percent-encoding is reported as not-a-member rather than guessed
 * at, because the cost of guessing wrong is a reader opening the wrong file. */
static int mm_is_member(struct mm_render *r, const mcdf_micro_slice *dest) {
  char path[MCDF_MICRO_LINE_MAX];
  mcdf_micro_member_info info;
  size_t i;

  if (dest->len == 0 || dest->len + 1 > sizeof path) return 0;
  for (i = 0; i < dest->len; ++i) path[i] = dest->text[i];
  path[dest->len] = '\0';

  if (!mcdf_micro_path_is_safe(path)) return 0;
  return mcdf_micro_member(r->reader, path, &info) == MCDF_MICRO_OK;
}

static int mm_enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
  struct mm_render *r = (struct mm_render *)userdata;
  mcdf_micro_link_detail link;
  const void *out = NULL;
  mcdf_micro_span span;

  if (mm_flush_held(r) != 0) return 1;

  switch (type) {
    case MD_SPAN_EM:     span = MCDF_MICRO_SPAN_EM;     break;
    case MD_SPAN_STRONG: span = MCDF_MICRO_SPAN_STRONG; break;
    case MD_SPAN_CODE:   span = MCDF_MICRO_SPAN_CODE;   break;
    case MD_SPAN_A: {
      const MD_SPAN_A_DETAIL *d = (const MD_SPAN_A_DETAIL *)detail;
      span = MCDF_MICRO_SPAN_LINK;
      link.href = mm_attr(&d->href);
      link.title = mm_attr(&d->title);
      link.is_member = mm_is_member(r, &link.href);
      out = &link;
      break;
    }
    case MD_SPAN_IMG: {
      const MD_SPAN_IMG_DETAIL *d = (const MD_SPAN_IMG_DETAIL *)detail;
      span = MCDF_MICRO_SPAN_IMAGE;
      link.href = mm_attr(&d->src);
      link.title = mm_attr(&d->title);
      link.is_member = mm_is_member(r, &link.href);
      out = &link;
      break;
    }
    default:
      return 1; /* an extension span; see mm_enter_block */
  }

  if (r->cb->enter_span == NULL) return 0;
  return mm_cb(r, r->cb->enter_span(r->ctx, span, out));
}

static int mm_leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
  struct mm_render *r = (struct mm_render *)userdata;
  mcdf_micro_link_detail link;
  const void *out = NULL;
  mcdf_micro_span span;

  if (mm_flush_held(r) != 0) return 1;

  switch (type) {
    case MD_SPAN_EM:     span = MCDF_MICRO_SPAN_EM;     break;
    case MD_SPAN_STRONG: span = MCDF_MICRO_SPAN_STRONG; break;
    case MD_SPAN_CODE:   span = MCDF_MICRO_SPAN_CODE;   break;
    case MD_SPAN_A: {
      const MD_SPAN_A_DETAIL *d = (const MD_SPAN_A_DETAIL *)detail;
      span = MCDF_MICRO_SPAN_LINK;
      link.href = mm_attr(&d->href);
      link.title = mm_attr(&d->title);
      link.is_member = mm_is_member(r, &link.href);
      out = &link;
      break;
    }
    case MD_SPAN_IMG: {
      const MD_SPAN_IMG_DETAIL *d = (const MD_SPAN_IMG_DETAIL *)detail;
      span = MCDF_MICRO_SPAN_IMAGE;
      link.href = mm_attr(&d->src);
      link.title = mm_attr(&d->title);
      link.is_member = mm_is_member(r, &link.href);
      out = &link;
      break;
    }
    default:
      return 1;
  }

  if (r->cb->leave_span == NULL) return 0;
  return mm_cb(r, r->cb->leave_span(r->ctx, span, out));
}

static int mm_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                   void *userdata) {
  struct mm_render *r = (struct mm_render *)userdata;
  int mapped;

  switch (type) {
    case MD_TEXT_NORMAL:   mapped = MCDF_MICRO_TEXT_NORMAL;   break;
    case MD_TEXT_NULLCHAR: mapped = MCDF_MICRO_TEXT_NULLCHAR; break;
    case MD_TEXT_BR:       mapped = MCDF_MICRO_TEXT_BR;       break;
    case MD_TEXT_SOFTBR:   mapped = MCDF_MICRO_TEXT_SOFTBR;   break;
    case MD_TEXT_ENTITY:   mapped = MCDF_MICRO_TEXT_ENTITY;   break;
    case MD_TEXT_CODE:     mapped = MCDF_MICRO_TEXT_CODE;     break;
    default:
      /* MD_TEXT_HTML cannot arrive: MD_FLAG_NOHTML makes raw HTML ordinary
       * text, which is what spec 10.4 requires. MD_TEXT_LATEXMATH needs an
       * extension that is not enabled. */
      return 1;
  }

  /* Outside a heading there is nothing to look ahead for. */
  if (!r->in_heading) return mm_emit_text(r, mapped, text, (size_t)size);

  if (mm_flush_held(r) != 0) return 1;
  r->held = mm_slice(text, size);
  r->held_type = mapped;
  r->have_held = 1;
  return 0;
}

/* ------------------------------------------------------------------ entry */

static mcdf_micro_status mm_content_info(mcdf_micro_reader *reader,
                                         mcdf_micro_member_info *out) {
  mcdf_micro_status st;
  if (reader == NULL || out == NULL) return MCDF_MICRO_E_INVAL;
  /* Ciphertext fed to a CommonMark parser yields confident nonsense rather
   * than an error, so the refusal has to come before the parse (spec 6). */
  if (mcdf_micro_is_sealed(reader, "content.md")) {
    return MCDF_MICRO_E_CONTENT_SEALED;
  }
  st = mcdf_micro_member(reader, "content.md", out);
  if (st != MCDF_MICRO_OK) return st;
  if (out->size > (uint64_t)UINT_MAX) return MCDF_MICRO_E_RANGE;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_render_size(mcdf_micro_reader *reader,
                                         size_t *out) {
  mcdf_micro_member_info info;
  mcdf_micro_status st;

  if (out == NULL) return MCDF_MICRO_E_INVAL;
  *out = 0;
  st = mm_content_info(reader, &info);
  if (st != MCDF_MICRO_OK) return st;
  if (info.size > (uint64_t)(size_t)-1) return MCDF_MICRO_E_RANGE;
  *out = (size_t)info.size;
  return MCDF_MICRO_OK;
}

/* Parses `len` bytes of `buffer` and streams the events. */
static mcdf_micro_status mm_parse(mcdf_micro_reader *reader, const void *buffer,
                                  size_t len,
                                  const mcdf_micro_render_callbacks *cb,
                                  void *ctx, int skip_doc_enter,
                                  int skip_doc_leave) {
  struct mm_render state;
  MD_PARSER parser;
  int rc;

  state.cb = cb;
  state.ctx = ctx;
  state.reader = reader;
  state.in_heading = 0;
  state.have_held = 0;
  state.aborted = 0;
  state.held_type = MCDF_MICRO_TEXT_NORMAL;
  state.held.text = NULL;
  state.held.len = 0;
  state.skip_doc_enter = skip_doc_enter;
  state.skip_doc_leave = skip_doc_leave;

  parser.abi_version = 0;
  /* MCDF fixes the dialect: CommonMark with no extensions, and raw HTML as
   * text rather than markup (spec 10.4). MD_FLAG_NOHTML is what makes the
   * second true, and it is the same flag the reference renderer uses - the two
   * implementations have to agree about what the document *is* before they can
   * disagree about anything else. */
  parser.flags = MD_FLAG_NOHTML;
  parser.enter_block = mm_enter_block;
  parser.leave_block = mm_leave_block;
  parser.enter_span = mm_enter_span;
  parser.leave_span = mm_leave_span;
  parser.text = mm_text;
  parser.debug_log = NULL;
  parser.syntax = NULL;

  rc = md_parse((const MD_CHAR *)buffer, (MD_SIZE)len, &parser, &state);
  if (rc != 0) {
    /* md4c returns non-zero both when a callback stopped the parse and when it
     * ran out of memory. The two are told apart by whether a callback said so,
     * because a caller that asked to stop should not be told the document is
     * broken. */
    return state.aborted ? MCDF_MICRO_E_ABORTED : MCDF_MICRO_E_PARSE;
  }
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_render(mcdf_micro_reader *reader, void *buffer,
                                    size_t buffer_len,
                                    const mcdf_micro_render_callbacks *cb,
                                    void *ctx) {
  mcdf_micro_member_info info;
  mcdf_micro_status st;

  if (cb == NULL || (buffer == NULL && buffer_len != 0)) {
    return MCDF_MICRO_E_INVAL;
  }
  st = mm_content_info(reader, &info);
  if (st != MCDF_MICRO_OK) return st;
  if ((uint64_t)buffer_len < info.size) return MCDF_MICRO_E_RANGE;

  st = mcdf_micro_read_at(reader, "content.md", 0, buffer, (size_t)info.size);
  if (st != MCDF_MICRO_OK) return st;

  return mm_parse(reader, buffer, (size_t)info.size, cb, ctx, 0, 0);
}

/* ---------------------------------------------------- a window at a time */

/* A reader draws a screenful at a time and should not have to hold a novel to
 * do it. md4c needs whatever it parses to be contiguous, which is not the same
 * as needing the document to be - so the walk below hands it one top-level
 * block group at a time and the buffer is sized by the largest block, not by
 * the document.
 *
 * Two things make the windowed events identical to the whole-document ones
 * rather than merely similar:
 *
 *   1. A window ends only at a top-level block boundary, so no list, quote or
 *      fence is ever cut in half.
 *   2. Link reference definitions are collected once and carried into every
 *      window. They are the only construct in CommonMark that reaches across a
 *      document, and they emit no events of their own - so prepending them is
 *      invisible in the stream and `[text][ref]` resolves in a window that does
 *      not contain the definition. */

/* True for a line that is blank or contains only spaces and tabs. */
static int mm_line_blank(const unsigned char *p, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    if (p[i] != ' ' && p[i] != '\t' && p[i] != '\r') return 0;
  }
  return 1;
}

/* Length of the line starting at `p`, excluding its newline; `*advance`
 * receives the bytes to step over including it. */
static size_t mm_line_at(const unsigned char *p, size_t avail,
                         size_t *advance) {
  size_t n = 0;
  while (n < avail && p[n] != '\n') ++n;
  *advance = (n < avail) ? n + 1u : n;
  return n;
}

/* A fence opener/closer at the start of a line: three or more ` or ~ after at
 * most three spaces. Returns the character, or 0. */
static char mm_fence_line(const unsigned char *p, size_t len, size_t *run) {
  size_t indent = 0, n = 0;
  char ch;
  while (indent < len && p[indent] == ' ') ++indent;
  if (indent > 3 || indent >= len) return '\0';
  ch = (char)p[indent];
  if (ch != '`' && ch != '~') return '\0';
  while (indent + n < len && (char)p[indent + n] == ch) ++n;
  if (n < 3) return '\0';
  *run = n;
  return ch;
}

/* Could this line start a new top-level block that is safe to break before?
 * A list marker or a `>` is excluded on purpose: a blank line inside a list or
 * a block quote is still inside it, and breaking there would end the construct
 * early and render it as two. That is what makes "the largest block must fit"
 * the buffer's real requirement - a long list counts as one block. */
static int mm_starts_top_block(const unsigned char *p, size_t len) {
  size_t indent = 0;
  char ch;
  while (indent < len && p[indent] == ' ') ++indent;
  if (indent > 3 || indent >= len) return 0;
  ch = (char)p[indent];
  if (ch == '>') return 0;
  if (ch == '-' || ch == '+' || ch == '*') {
    const size_t next = indent + 1u;
    if (next >= len || p[next] == ' ' || p[next] == '\t') return 0;
  }
  if (ch >= '0' && ch <= '9') {
    size_t i = indent;
    while (i < len && p[i] >= '0' && p[i] <= '9') ++i;
    if (i < len && (p[i] == '.' || p[i] == ')')) {
      const size_t next = i + 1u;
      if (next >= len || p[next] == ' ' || p[next] == '\t') return 0;
    }
  }
  return 1;
}

/* The largest prefix of `window` that ends at a top-level block boundary, or 0
 * if there is none. `is_tail` means the window reaches the end of the document,
 * in which case all of it is parseable. */
static size_t mm_window_end(const unsigned char *window, size_t len,
                            int is_tail) {
  size_t at = 0, cut = 0;
  size_t fence_run = 0;
  char fence = '\0';
  int pending_blank = 0;

  if (is_tail) return len;

  while (at < len) {
    size_t advance = 0;
    const size_t line = mm_line_at(window + at, len - at, &advance);
    if (advance == 0) break;
    if (at + advance >= len && line == advance) {
      /* The window ends mid-line. Its first bytes are still enough to say
       * whether it opens a top-level block, which is all a boundary decision
       * needs - and using them is what lets a window barely larger than one
       * block still make progress instead of reporting E_RANGE. */
      if (fence == '\0' && pending_blank &&
          mm_starts_top_block(window + at, line)) {
        cut = at;
      }
      break;
    }

    if (fence != '\0') {
      size_t run = 0;
      if (mm_fence_line(window + at, line, &run) == fence && run >= fence_run) {
        fence = '\0';
      }
      pending_blank = 0;
    } else {
      size_t run = 0;
      const char opened = mm_fence_line(window + at, line, &run);
      if (opened != '\0') {
        fence = opened;
        fence_run = run;
        pending_blank = 0;
      } else if (mm_line_blank(window + at, line)) {
        pending_blank = 1;
      } else {
        /* A new top-level block after a blank line: everything before the
         * blank run is a complete set of blocks. */
        if (pending_blank && mm_starts_top_block(window + at, line)) cut = at;
        pending_blank = 0;
      }
    }
    at += advance;
  }
  return cut;
}

/* Copies every link reference definition in content.md to the front of the
 * buffer. Streamed a line at a time, so this costs no buffer of its own.
 *
 * Deliberately generous: a line that merely looks like `[label]: rest` is
 * carried. Carrying one that md4c would not have treated as a definition is
 * harmless, because md4c decides for itself and a non-definition contributes no
 * events either. Missing one would not be harmless - a link would silently
 * fail to resolve in one window and resolve in another. */
static mcdf_micro_status mm_collect_refs(mcdf_micro_reader *reader,
                                         unsigned char *buffer, size_t cap,
                                         size_t *out_len) {
  struct mm_cursor c;
  char line[MCDF_MICRO_LINE_MAX];
  size_t used = 0;
  char fence = '\0';
  size_t fence_run = 0;
  mcdf_micro_status st;

  *out_len = 0;
  st = mm_cursor_open(reader, "content.md", &c);
  if (st != MCDF_MICRO_OK) return st;

  for (;;) {
    size_t len = 0, i, close;
    int truncated = 0;

    st = mm_line(&c, line, sizeof line, &len, &truncated);
    if (st == MCDF_MICRO_E_NOT_FOUND) break;
    if (st != MCDF_MICRO_OK) return st;
    /* A definition cannot be longer than one line buffer; an over-long line is
     * not one, so skipping it is right rather than merely convenient. */
    if (truncated) continue;

    {
      size_t run = 0;
      const char f = mm_fence_line((const unsigned char *)line, len, &run);
      if (fence != '\0') {
        if (f == fence && run >= fence_run) fence = '\0';
        continue;
      }
      if (f != '\0') {
        fence = f;
        fence_run = run;
        continue;
      }
    }

    i = 0;
    while (i < len && line[i] == ' ') ++i;
    if (i > 3 || i >= len || line[i] != '[') continue;
    close = 0;
    for (++i; i < len; ++i) {
      if (line[i] == '\\') { ++i; continue; }
      if (line[i] == ']') { close = i; break; }
    }
    if (close == 0 || close + 1 >= len || line[close + 1] != ':') continue;

    if (used + len + 1u > cap) return MCDF_MICRO_E_RANGE;
    for (i = 0; i < len; ++i) buffer[used + i] = (unsigned char)line[i];
    buffer[used + len] = '\n';
    used += len + 1u;
  }

  /* A blank line after the carried definitions, so the first block of a window
   * can never be read as a continuation of the last definition. */
  if (used > 0) {
    if (used + 1u > cap) return MCDF_MICRO_E_RANGE;
    buffer[used++] = '\n';
  }
  *out_len = used;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_render_begin(mcdf_micro_reader *reader,
                                          void *buffer, size_t buffer_len,
                                          mcdf_micro_render_iter *iter) {
  mcdf_micro_member_info info;
  mcdf_micro_status st;
  size_t prefix = 0;

  if (iter == NULL || buffer == NULL) return MCDF_MICRO_E_INVAL;
  st = mm_content_info(reader, &info);
  if (st != MCDF_MICRO_OK) return st;

  st = mm_collect_refs(reader, (unsigned char *)buffer, buffer_len, &prefix);
  if (st != MCDF_MICRO_OK) return st;
  /* Room for the definitions is not room to work in. */
  if (prefix + 1u >= buffer_len) return MCDF_MICRO_E_RANGE;

  iter->reader = reader;
  iter->buffer = (unsigned char *)buffer;
  iter->buffer_len = buffer_len;
  iter->prefix_len = prefix;
  iter->at = 0;
  iter->size = info.size;
  iter->started = 1;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_render_next(mcdf_micro_render_iter *iter,
                                         const mcdf_micro_render_callbacks *cb,
                                         void *ctx, int *done) {
  size_t room, want, end;
  uint64_t remain;
  mcdf_micro_status st;
  int is_tail;

  if (iter == NULL || cb == NULL || done == NULL || !iter->started) {
    return MCDF_MICRO_E_INVAL;
  }
  *done = 0;
  if (iter->at >= iter->size) {
    *done = 1;
    return MCDF_MICRO_OK;
  }

  room = iter->buffer_len - iter->prefix_len;
  remain = iter->size - iter->at;
  want = (remain < (uint64_t)room) ? (size_t)remain : room;
  is_tail = ((uint64_t)want == remain);

  st = mcdf_micro_read_at(iter->reader, "content.md", iter->at,
                          iter->buffer + iter->prefix_len, want);
  if (st != MCDF_MICRO_OK) return st;

  end = mm_window_end(iter->buffer + iter->prefix_len, want, is_tail);
  if (end == 0) {
    /* One top-level block is larger than the buffer. Say so, rather than cut
     * it somewhere arbitrary and emit a parse of something the document does
     * not contain. */
    return MCDF_MICRO_E_RANGE;
  }

  /* One document, however many windows it takes to read: opened at the first,
   * closed after the last. */
  {
    const int first = (iter->at == 0);
    const int last = (iter->at + (uint64_t)end >= iter->size);
    st = mm_parse(iter->reader, iter->buffer, iter->prefix_len + end, cb, ctx,
                  !first, !last);
  }
  if (st != MCDF_MICRO_OK) return st;

  iter->at += (uint64_t)end;
  if (iter->at >= iter->size) *done = 1;
  return MCDF_MICRO_OK;
}

#else /* the gate removes the code, not just the calls - md4c is not compiled */

mcdf_micro_status mcdf_micro_render_size(mcdf_micro_reader *reader,
                                         size_t *out) {
  (void)reader;
  if (out != NULL) *out = 0;
  return MCDF_MICRO_E_DISABLED;
}

mcdf_micro_status mcdf_micro_render(mcdf_micro_reader *reader, void *buffer,
                                    size_t buffer_len,
                                    const mcdf_micro_render_callbacks *cb,
                                    void *ctx) {
  (void)reader; (void)buffer; (void)buffer_len; (void)cb; (void)ctx;
  return MCDF_MICRO_E_DISABLED;
}

mcdf_micro_status mcdf_micro_render_begin(mcdf_micro_reader *reader,
                                          void *buffer, size_t buffer_len,
                                          mcdf_micro_render_iter *iter) {
  (void)reader; (void)buffer; (void)buffer_len;
  if (iter != NULL) iter->started = 0;
  return MCDF_MICRO_E_DISABLED;
}

mcdf_micro_status mcdf_micro_render_next(mcdf_micro_render_iter *iter,
                                         const mcdf_micro_render_callbacks *cb,
                                         void *ctx, int *done) {
  (void)iter; (void)cb; (void)ctx;
  if (done != NULL) *done = 1;
  return MCDF_MICRO_E_DISABLED;
}

#endif /* MCDF_MICRO_ENABLE_RENDER */
