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

mcdf_micro_status mcdf_micro_render(mcdf_micro_reader *reader, void *buffer,
                                    size_t buffer_len,
                                    const mcdf_micro_render_callbacks *cb,
                                    void *ctx) {
  mcdf_micro_member_info info;
  struct mm_render state;
  MD_PARSER parser;
  mcdf_micro_status st;
  int rc;

  if (cb == NULL || (buffer == NULL && buffer_len != 0)) {
    return MCDF_MICRO_E_INVAL;
  }
  st = mm_content_info(reader, &info);
  if (st != MCDF_MICRO_OK) return st;
  if ((uint64_t)buffer_len < info.size) return MCDF_MICRO_E_RANGE;

  st = mcdf_micro_read_at(reader, "content.md", 0, buffer, (size_t)info.size);
  if (st != MCDF_MICRO_OK) return st;

  state.cb = cb;
  state.ctx = ctx;
  state.reader = reader;
  state.in_heading = 0;
  state.have_held = 0;
  state.aborted = 0;
  state.held_type = MCDF_MICRO_TEXT_NORMAL;
  state.held.text = NULL;
  state.held.len = 0;

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

  rc = md_parse((const MD_CHAR *)buffer, (MD_SIZE)info.size, &parser, &state);
  if (rc != 0) {
    /* md4c returns non-zero both when a callback stopped the parse and when it
     * ran out of memory. The two are told apart by whether a callback said so,
     * because a caller that asked to stop should not be told the document is
     * broken. */
    return state.aborted ? MCDF_MICRO_E_ABORTED : MCDF_MICRO_E_PARSE;
  }
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

#endif /* MCDF_MICRO_ENABLE_RENDER */
