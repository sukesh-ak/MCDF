/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */
#ifndef MCDF_MICRO_RENDER_H
#define MCDF_MICRO_RENDER_H

/*
 * mcdf_micro - content.md as a block/span event stream.
 *
 * This is the seam between "what the document says" and "what the screen
 * shows". It parses CommonMark and hands out structure; it draws nothing,
 * measures nothing, and knows about no font, panel or pixel. A layout engine
 * consumes these events and decides all of that.
 *
 * Three things it does that a bare parser would not:
 *
 *   1. **It hides md4c.** Every type here is mcdf_micro_*, so the parser can be
 *      replaced without touching a caller, and no vendor header reaches
 *      firmware that includes this one.
 *   2. **It resolves the container.** A link or image destination is reported
 *      with a flag saying whether it names a member of *this* container, so a
 *      layout engine can pull the bytes with mcdf_micro_read_at() instead of
 *      guessing what the path means.
 *   3. **It refuses sealed content.** Ciphertext fed to a CommonMark parser
 *      yields confident nonsense rather than an error, so a sealed content.md
 *      is refused outright (spec 6).
 *
 * The whole file is behind MCDF_MICRO_ENABLE_RENDER. When that gate is off the
 * declarations stay - so one piece of caller code compiles against every
 * configuration - the bodies return MCDF_MICRO_E_DISABLED, and md4c is not
 * compiled at all. Callers that branch at compile time test
 * MCDF_MICRO_HAS_RENDER.
 *
 * ---------------------------------------------------------------------------
 * MEMORY: a screen is not a document.
 *
 * md4c parses a contiguous buffer, so whatever you ask it to parse has to be
 * in RAM. That is *not* the same as needing the whole document: a reader draws
 * thirty lines at a time and has no business holding a novel to do it.
 *
 * So there are two ways in, and the constrained one is the general one:
 *
 *   - mcdf_micro_render() parses the whole of content.md at once. Simple, and
 *     right on a host or a part with PSRAM to spare.
 *   - mcdf_micro_render_begin()/_next() walk the document a window at a time.
 *     The buffer only has to hold the largest single top-level block - a
 *     paragraph, a list, a code block - not the document.
 *
 * The windowed form is exact, not an approximation. CommonMark has one
 * construct that reaches across a document, the link reference definition
 * (`[ref]: /url`), and _begin() collects those in a streaming pre-pass and
 * carries them into every window. A definition emits no events of its own, so
 * a caller sees precisely the window's own events with its links resolved -
 * the same events, in the same order, as parsing the whole document would
 * produce.
 *
 * What does not change: the caller owns the buffer, and md4c allocates
 * internally (its block stack and mark chain), so a build with this gate on
 * needs a heap where a Core-only build needs none. That last one is not
 * avoidable with a real CommonMark parser, and it is why the gate exists.
 */

#include <stddef.h>
#include <stdint.h>

#include "mcdf_micro/mcdf_micro.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MCDF_MICRO_ENABLE_RENDER
#define MCDF_MICRO_HAS_RENDER 1
#endif

/* ------------------------------------------------------------------ blocks */

/* Only the blocks CommonMark produces with no extensions. MCDF fixes the
 * dialect (spec 10.4): no tables, task lists, strikethrough or footnotes, and
 * raw HTML is ordinary text rather than markup - so there is no HTML block
 * here, and a document containing `<div>x</div>` reports it as a paragraph
 * whose text happens to look like a tag. */
typedef enum mcdf_micro_block {
  MCDF_MICRO_BLOCK_DOC = 0,
  MCDF_MICRO_BLOCK_QUOTE,
  MCDF_MICRO_BLOCK_UL,
  MCDF_MICRO_BLOCK_OL,
  MCDF_MICRO_BLOCK_LI,
  MCDF_MICRO_BLOCK_HR,
  MCDF_MICRO_BLOCK_H,
  MCDF_MICRO_BLOCK_CODE,
  MCDF_MICRO_BLOCK_P
} mcdf_micro_block;

typedef enum mcdf_micro_span {
  MCDF_MICRO_SPAN_EM = 0,
  MCDF_MICRO_SPAN_STRONG,
  MCDF_MICRO_SPAN_LINK,
  MCDF_MICRO_SPAN_IMAGE,
  MCDF_MICRO_SPAN_CODE
} mcdf_micro_span;

typedef enum mcdf_micro_text_type {
  MCDF_MICRO_TEXT_NORMAL = 0,
  MCDF_MICRO_TEXT_NULLCHAR, /* U+0000; CommonMark replaces it with U+FFFD */
  MCDF_MICRO_TEXT_BR,       /* hard break */
  MCDF_MICRO_TEXT_SOFTBR,   /* newline that is not semantically meaningful */
  MCDF_MICRO_TEXT_ENTITY,   /* verbatim &nbsp; / &#1234; - never decoded here */
  MCDF_MICRO_TEXT_CODE      /* inside a code span or code block */
} mcdf_micro_text_type;

/* ----------------------------------------------------------------- details */

/* A run of bytes inside the caller's document buffer. Never NUL-terminated and
 * never copied: it points into the buffer and is valid only until
 * mcdf_micro_render() returns. */
typedef struct mcdf_micro_slice {
  const char *text;
  size_t      len;
} mcdf_micro_slice;

typedef struct mcdf_micro_heading_detail {
  int level; /* 1-6 */

  /* The `{#id}` anchor, and the one field here that is **only valid on
   * leave_block**. The anchor sits at the end of the heading's text, so it
   * cannot be known when the heading opens - md4c has not read it yet. Rather
   * than buffer the heading to fake it, this reports the truth: `id.len` is 0
   * on enter and carries the anchor on leave.
   *
   * Every heading is reported, nested ones included, and each carries its own
   * anchor: that is spec 10.4, and it is deliberately *not* the rule that
   * decides schema binding. Only a top-level heading binds a section (spec
   * 4.2), which mcdf_micro_has_anchor() answers without a parser. Two
   * questions, two rules; do not use one for the other. */
  mcdf_micro_slice id;
} mcdf_micro_heading_detail;

typedef struct mcdf_micro_list_detail {
  int      is_tight;  /* non-zero when the list is tight */
  unsigned start;     /* ordered lists only: the first item's number */
  char     mark;      /* '-', '+', '*', or the '.'/')' of an ordered list */
} mcdf_micro_list_detail;

typedef struct mcdf_micro_code_detail {
  mcdf_micro_slice info;  /* the fence info string, e.g. "c" - may be empty */
  char             fence; /* '`' or '~'; 0 for an indented code block */
} mcdf_micro_code_detail;

typedef struct mcdf_micro_link_detail {
  mcdf_micro_slice href;  /* destination, exactly as written */
  mcdf_micro_slice title; /* may be empty */

  /* Non-zero when `href` names a member of this container that is safe to use
   * as a relative path. That is what spec 4.1 asks of an asset reference, and
   * what lets a layout engine call mcdf_micro_read_at(href) for an image
   * instead of interpreting the path itself. Zero for an external URL, for a
   * path that escapes the container, and for a destination this reader cannot
   * match to a member verbatim (a fragment or percent-encoding, say) - in
   * every one of those cases the caller must not treat it as container data.
   *
   * A renderer MUST NOT fetch an external resource (spec 4.1); this flag is
   * how it tells the difference. */
  int is_member;
} mcdf_micro_link_detail;

/* --------------------------------------------------------------- callbacks */

/* Every callback returns 0 to continue or non-zero to stop the render, in
 * which case mcdf_micro_render() returns MCDF_MICRO_E_ABORTED. None may throw
 * or longjmp across the boundary, and none may call back into the reader with
 * a different member - the document buffer is in use.
 *
 * Any callback may be NULL; a NULL one is simply not called, so a caller that
 * only wants text can set only `text`.
 *
 * The `detail` pointer is NULL for blocks and spans that carry none, and is
 * valid only for the duration of the call. */
typedef struct mcdf_micro_render_callbacks {
  int (*enter_block)(void *ctx, mcdf_micro_block type, const void *detail);
  int (*leave_block)(void *ctx, mcdf_micro_block type, const void *detail);
  int (*enter_span)(void *ctx, mcdf_micro_span type, const void *detail);
  int (*leave_span)(void *ctx, mcdf_micro_span type, const void *detail);
  int (*text)(void *ctx, mcdf_micro_text_type type, const char *text,
              size_t len);
} mcdf_micro_render_callbacks;

/* --------------------------------------------------- whole document at once */

/* Bytes needed to parse content.md in one go: its size, exactly. Useful for
 * deciding whether the simple path fits; a device that knows it does not
 * should use the windowed form and never ask.
 *
 * E_NOT_FOUND when there is no content.md, E_CONTENT_SEALED when it is
 * ciphertext. */
mcdf_micro_status mcdf_micro_render_size(mcdf_micro_reader *reader,
                                         size_t *out);

/* Reads content.md into `buffer` and streams all of it to `cb`.
 *
 * `buffer` must be at least mcdf_micro_render_size() bytes; a smaller one is
 * E_RANGE rather than a truncated parse. The buffer belongs to the caller
 * before and after - nothing is retained, and every slice handed to a callback
 * points into it.
 *
 * Returns E_CONTENT_SEALED for a sealed content.md (spec 6), E_ABORTED when a
 * callback asked to stop, E_PARSE if the parser fails, E_DISABLED when the
 * gate is off. */
mcdf_micro_status mcdf_micro_render(mcdf_micro_reader *reader, void *buffer,
                                    size_t buffer_len,
                                    const mcdf_micro_render_callbacks *cb,
                                    void *ctx);

/* ------------------------------------------------------ a window at a time */

/* Walks content.md in windows, for a caller whose RAM is sized by its screen
 * rather than by the documents it may be handed.
 *
 * The state is public so it can live on the stack - the core never allocates -
 * but the fields are an implementation detail; only the two calls below may
 * touch them. */
typedef struct mcdf_micro_render_iter {
  mcdf_micro_reader *reader;
  unsigned char     *buffer;
  size_t             buffer_len;
  size_t             prefix_len; /* carried link reference definitions */
  uint64_t           at;         /* next byte of content.md to parse */
  uint64_t           size;       /* content.md's total size */
  int                started;
} mcdf_micro_render_iter;

/* Prepares a walk over content.md.
 *
 * Streams the document once to collect its link reference definitions and
 * copies them to the front of `buffer`, where they stay for the whole walk -
 * that is what makes a windowed parse produce the same events as a whole
 * document one. `buffer` must outlive the iterator and must not be touched
 * between calls.
 *
 * E_RANGE if the definitions alone do not leave usable room, and the same
 * E_NOT_FOUND / E_CONTENT_SEALED as the whole-document form. */
mcdf_micro_status mcdf_micro_render_begin(mcdf_micro_reader *reader,
                                          void *buffer, size_t buffer_len,
                                          mcdf_micro_render_iter *iter);

/* Streams the next window's events and advances.
 *
 * Sets `*done` non-zero when the document is exhausted, at which point no
 * events were emitted for this call. A window ends at a top-level block
 * boundary, never inside one, so a list, block quote or fenced code block is
 * never split across two windows - and the events are identical to those a
 * whole-document parse would produce.
 *
 * The consequence of that guarantee is the buffer's real requirement: it must
 * hold the largest single top-level block, not the document. A block that does
 * not fit is E_RANGE - the caller needs a bigger buffer, and is told so rather
 * than handed a truncated parse.
 *
 * On any error the walk stops and cannot be resumed. Events already delivered
 * by *earlier* calls stand - they were correct when they were made, and a
 * stream cannot un-emit - but this call emits nothing before it fails: the
 * window is checked whole before a byte of it is parsed. */
mcdf_micro_status mcdf_micro_render_next(mcdf_micro_render_iter *iter,
                                         const mcdf_micro_render_callbacks *cb,
                                         void *ctx, int *done);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MCDF_MICRO_RENDER_H */
