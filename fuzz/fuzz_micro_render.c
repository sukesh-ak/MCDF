/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* Fuzzes mcdf_micro's block/span event stream over raw markdown.
 *
 * Its own target rather than a case in fuzz_micro_member, because this is the
 * one part of the reader that allocates: md4c manages a heap internally, and
 * the wiring around it holds a lookahead slice into the document buffer so a
 * heading's trailing `{#id}` can be split off. A stale pointer there would be
 * a use-after-free that only shows up on a document shaped a particular way -
 * which is exactly what a fuzzer with a markdown corpus is for.
 *
 * The callbacks are not no-ops: they read every slice they are handed. A
 * harness that ignored them would never catch a slice pointing outside the
 * buffer, which is the failure this is here to find. */

#include <stdio.h>
#include <string.h>

#include "mcdf_micro/mcdf_micro.h"
#include "mcdf_micro/mcdf_micro_render.h"

#include "driver.h"

#define BLOCK    512u
#define MAX_BODY (256u * 1024u)

static unsigned char g_archive[MAX_BODY + 4u * BLOCK];
static size_t        g_archive_len;
static unsigned char g_arena[MCDF_MICRO_ARENA_SIZE(16, 1024)];
static unsigned char g_doc[MAX_BODY];

/* Touched by every callback so a bad slice is a read the sanitizer sees. */
static volatile unsigned long g_sink;

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

static void put_octal(unsigned char *field, size_t width, unsigned long value) {
  size_t i;
  field[width - 1] = '\0';
  for (i = width - 1; i > 0; --i) {
    field[i - 1] = (unsigned char)('0' + (value & 7u));
    value >>= 3;
  }
}

static void eat(const char *text, size_t len) {
  size_t i;
  unsigned long sum = 0;
  if (text == NULL) return;
  for (i = 0; i < len; ++i) sum += (unsigned char)text[i];
  g_sink += sum;
}

static int on_block(void *ctx, mcdf_micro_block type, const void *detail) {
  (void)ctx;
  if (type == MCDF_MICRO_BLOCK_H && detail != NULL) {
    const mcdf_micro_heading_detail *d =
        (const mcdf_micro_heading_detail *)detail;
    g_sink += (unsigned long)d->level;
    eat(d->id.text, d->id.len);
  } else if (type == MCDF_MICRO_BLOCK_CODE && detail != NULL) {
    const mcdf_micro_code_detail *d = (const mcdf_micro_code_detail *)detail;
    eat(d->info.text, d->info.len);
    g_sink += (unsigned long)(unsigned char)d->fence;
  } else if ((type == MCDF_MICRO_BLOCK_UL || type == MCDF_MICRO_BLOCK_OL) &&
             detail != NULL) {
    const mcdf_micro_list_detail *d = (const mcdf_micro_list_detail *)detail;
    g_sink += (unsigned long)d->start + (unsigned long)d->is_tight;
  }
  return 0;
}

static int on_span(void *ctx, mcdf_micro_span type, const void *detail) {
  (void)ctx;
  if ((type == MCDF_MICRO_SPAN_LINK || type == MCDF_MICRO_SPAN_IMAGE) &&
      detail != NULL) {
    const mcdf_micro_link_detail *d = (const mcdf_micro_link_detail *)detail;
    eat(d->href.text, d->href.len);
    eat(d->title.text, d->title.len);
    g_sink += (unsigned long)d->is_member;
  }
  return 0;
}

static int on_text(void *ctx, mcdf_micro_text_type type, const char *text,
                   size_t len) {
  (void)ctx;
  g_sink += (unsigned long)type;
  eat(text, len);
  return 0;
}

/* content.md plus one asset, so link resolution has something to match and
 * `is_member` is exercised in both directions. */
static void build(const uint8_t *data, size_t size) {
  static const char kAsset[] = "assets/d.png";
  unsigned char *h;
  unsigned long sum;
  size_t i, blocks, at = 0;

  memset(g_archive, 0, sizeof g_archive);

  h = g_archive;
  memcpy(h, "content.md", 10);
  put_octal(h + 100, 8, 0644);
  put_octal(h + 124, 12, (unsigned long)size);
  put_octal(h + 136, 12, 0);
  h[156] = '0';
  memcpy(h + 257, "ustar", 5);
  h[263] = '0';
  h[264] = '0';
  memset(h + 148, ' ', 8);
  sum = 0;
  for (i = 0; i < BLOCK; ++i) sum += h[i];
  snprintf((char *)h + 148, 7, "%06lo", sum);
  h[154] = '\0';
  h[155] = ' ';
  blocks = (size + BLOCK - 1u) / BLOCK;
  memcpy(g_archive + BLOCK, data, size);
  at = BLOCK + blocks * BLOCK;

  h = g_archive + at;
  memcpy(h, kAsset, sizeof kAsset - 1);
  put_octal(h + 100, 8, 0644);
  put_octal(h + 124, 12, 4);
  put_octal(h + 136, 12, 0);
  h[156] = '0';
  memcpy(h + 257, "ustar", 5);
  h[263] = '0';
  h[264] = '0';
  memset(h + 148, ' ', 8);
  sum = 0;
  for (i = 0; i < BLOCK; ++i) sum += h[i];
  snprintf((char *)h + 148, 7, "%06lo", sum);
  h[154] = '\0';
  h[155] = ' ';
  memcpy(g_archive + at + BLOCK, "png\n", 4);
  at += 2u * BLOCK;

  g_archive_len = at + 2u * BLOCK;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  struct span span;
  mcdf_micro_source src;
  mcdf_micro_reader *reader = NULL;
  mcdf_micro_render_callbacks cb;
  size_t need = 0;

  if (size > MAX_BODY - 4u * BLOCK) return 0;
  build(data, size);

  span.bytes = g_archive;
  span.len = g_archive_len;
  src.ctx = &span;
  src.read = span_read;
  src.size = (uint64_t)g_archive_len;

  if (mcdf_micro_open(&src, g_arena, sizeof g_arena, &reader) != MCDF_MICRO_OK) {
    return 0;
  }

  cb.enter_block = on_block;
  cb.leave_block = on_block;
  cb.enter_span = on_span;
  cb.leave_span = on_span;
  cb.text = on_text;

  (void)mcdf_micro_render_size(reader, &need);
  (void)mcdf_micro_render(reader, g_doc, sizeof g_doc, &cb, NULL);

  /* The windowed walk, on a buffer far smaller than most inputs - so nearly
   * every one exercises the boundary scan, the carried reference definitions
   * and the seam between windows, which is where a slice into the buffer could
   * outlive the window it pointed into. */
  {
    static unsigned char window[1024];
    mcdf_micro_render_iter iter;
    int done = 0;
    unsigned guard;

    if (mcdf_micro_render_begin(reader, window, sizeof window, &iter) ==
        MCDF_MICRO_OK) {
      for (guard = 0; guard < 4096u && !done; ++guard) {
        if (mcdf_micro_render_next(&iter, &cb, NULL, &done) != MCDF_MICRO_OK) {
          break;
        }
      }
    }
  }

  /* Again with the callbacks omitted: a NULL callback must be skipped, not
   * called through. */
  cb.enter_block = NULL;
  cb.leave_block = NULL;
  cb.enter_span = NULL;
  cb.leave_span = NULL;
  (void)mcdf_micro_render(reader, g_doc, sizeof g_doc, &cb, NULL);

  mcdf_micro_close(reader);
  return 0;
}
