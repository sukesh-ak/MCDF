/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The reader object: one pass over the archive into a caller-owned arena,
 * then random access by path. Nothing here allocates. */

#include "internal.h"

/* Alignment the arena is rounded up to. Wide enough for every field the
 * reader stores; long double is left out deliberately, since nothing here is
 * floating point and it would cost 8 bytes of slack on some ABIs. */
union mm_align {
  void    *p;
  uint64_t u;
  double   d;
};
#define MM_ALIGN ((size_t)sizeof(union mm_align))

/* The published arena constants are upper bounds firmware sizes against, so
 * the build fails rather than the device if a struct outgrows one. */
MM_STATIC_ASSERT(sizeof(struct mm_entry) <= (size_t)MCDF_MICRO_ENTRY_SIZE,
                 entry_size_bound);
MM_STATIC_ASSERT(sizeof(struct mcdf_micro_reader) + 2u * sizeof(union mm_align) <=
                     (size_t)MCDF_MICRO_ARENA_OVERHEAD,
                 arena_overhead_bound);

static size_t mm_align_up(size_t n) {
  const size_t rem = n % MM_ALIGN;
  return rem == 0 ? n : n + (MM_ALIGN - rem);
}

/* ------------------------------------------------------------- small utils */

size_t mm_strlen(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

int mm_streq(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] != '\0' && a[i] == b[i]) ++i;
  return a[i] == b[i];
}

mcdf_micro_status mm_emit(const char *src, size_t len, char *dst,
                          size_t dst_len, size_t *out_len) {
  size_t i;
  if (out_len != NULL) *out_len = len;
  if (dst == NULL) return MCDF_MICRO_OK;  /* length query */
  if (dst_len == 0 || len + 1 > dst_len) return MCDF_MICRO_E_RANGE;
  for (i = 0; i < len; ++i) dst[i] = src[i];
  dst[len] = '\0';
  return MCDF_MICRO_OK;
}

/* ---------------------------------------------------------------- features */

uint32_t mcdf_micro_features(void) {
  /* The INTEGRITY / SIGNED / RENDER bits are set by the gates that compile the
   * code behind them, never by this file asserting a capability on their
   * behalf. */
  uint32_t bits = MCDF_MICRO_FEATURE_CORE;
#ifdef MCDF_MICRO_ENABLE_INTEGRITY
  bits |= MCDF_MICRO_FEATURE_INTEGRITY;
#endif
#ifdef MCDF_MICRO_ENABLE_SIGNED
  bits |= MCDF_MICRO_FEATURE_SIGNED;
#endif
#ifdef MCDF_MICRO_ENABLE_RENDER
  bits |= MCDF_MICRO_FEATURE_RENDER;
#endif
  return bits;
}

const char *mcdf_micro_status_str(mcdf_micro_status status) {
  switch (status) {
    case MCDF_MICRO_OK:              return "OK";
    case MCDF_MICRO_E_INVAL:         return "E_INVAL";
    case MCDF_MICRO_E_IO:            return "E_IO";
    case MCDF_MICRO_E_ARENA:         return "E_ARENA";
    case MCDF_MICRO_E_PARSE:         return "E_PARSE";
    case MCDF_MICRO_E_NOT_FOUND:     return "E_NOT_FOUND";
    case MCDF_MICRO_E_RANGE:         return "E_RANGE";
    /* Kit spellings, verbatim from conformance/errors.md. A harness greps for
     * these, so they are the one set of strings in this library that may not
     * be reworded. */
    case MCDF_MICRO_E_MISSING_CONTENT:   return "E_MISSING_CONTENT";
    case MCDF_MICRO_E_SCHEMA_UNBOUND:    return "E_SCHEMA_UNBOUND";
    case MCDF_MICRO_E_REQUIRED_SECTION_MISSING:
      return "E_REQUIRED_SECTION_MISSING";
    case MCDF_MICRO_E_ASSET_PATH_ESCAPE: return "E_ASSET_PATH_ESCAPE";
    case MCDF_MICRO_E_MISSING_MANIFEST:  return "E_MISSING_MANIFEST";
    case MCDF_MICRO_E_MANIFEST_HASH_MISMATCH:
      return "E_MANIFEST_HASH_MISMATCH";
    case MCDF_MICRO_E_MANIFEST_MISSING_FILE:
      return "E_MANIFEST_MISSING_FILE";
    case MCDF_MICRO_E_MANIFEST_EXTRA_FILE: return "E_MANIFEST_EXTRA_FILE";
    case MCDF_MICRO_E_ALGO_NOT_ALLOWED:  return "E_ALGO_NOT_ALLOWED";
    case MCDF_MICRO_E_CONTENT_SEALED:    return "E_CONTENT_SEALED";
    case MCDF_MICRO_E_ABORTED:           return "E_ABORTED";
    case MCDF_MICRO_E_DISABLED:          return "E_DISABLED";
    case MCDF_MICRO_E_UNIMPLEMENTED:     return "E_UNIMPLEMENTED";
  }
  return "E_UNKNOWN";
}

/* ------------------------------------------------------------ path safety */

int mcdf_micro_path_is_safe(const char *path) {
  size_t i = 0, seg = 0;

  if (path == NULL || path[0] == '\0') return 0;
  if (path[0] == '/') return 0;
  /* "C:\..." and "C:/..." are absolute on the one platform that spells it
   * that way; a container built there must not become one here. */
  if (path[1] == ':') return 0;

  for (;;) {
    const char c = path[i];
    if (c == '\0' || c == '/') {
      if (seg == 0) return 0;  /* empty component: leading, double or trailing / */
      if (seg == 2 && path[i - 2] == '.' && path[i - 1] == '.') return 0;
      if (c == '\0') break;
      seg = 0;
    } else if (c == '\\') {
      return 0;  /* never a separator here, and never safe as a literal */
    } else {
      ++seg;
    }
    ++i;
  }
  return 1;
}

/* ------------------------------------------------------------------- open */

static mcdf_micro_status mm_push(mcdf_micro_reader *r, const struct mm_header *h,
                                 uint64_t data_off) {
  const size_t need_head = sizeof(struct mm_entry);
  const size_t need_tail = h->name_len + 1u;
  struct mm_entry *e;
  size_t i;

  if (r->tail < need_tail) return MCDF_MICRO_E_ARENA;
  if (r->head + need_head > r->tail - need_tail) return MCDF_MICRO_E_ARENA;

  r->tail -= need_tail;
  for (i = 0; i < h->name_len; ++i) r->base[r->tail + i] = (unsigned char)h->name[i];
  r->base[r->tail + h->name_len] = '\0';

  e = &r->entries[r->count];
  e->data_off = data_off;
  e->size = h->size;
  e->name = (const char *)(r->base + r->tail);

  r->head += need_head;
  r->count += 1u;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_open(const mcdf_micro_source *src, void *arena,
                                  size_t arena_len, mcdf_micro_reader **out) {
  mcdf_micro_reader *r;
  unsigned char *raw, *base;
  size_t adjust, usable, header_bytes;
  uint64_t pos;
  int saw_end = 0;

  if (src == NULL || src->read == NULL || arena == NULL || out == NULL) {
    return MCDF_MICRO_E_INVAL;
  }
  /* A USTAR archive is a whole number of 512-byte blocks and carries two zero
   * blocks at the end, so anything shorter than 1024 or ragged is not one.
   * Refusing here turns a whole class of truncation into a clean error before
   * a single header is parsed. */
  if (src->size < 2u * MM_BLOCK || (src->size % MM_BLOCK) != 0u) {
    return MCDF_MICRO_E_PARSE;
  }

  raw = (unsigned char *)arena;
  adjust = (size_t)((uintptr_t)raw % MM_ALIGN);
  if (adjust != 0) adjust = MM_ALIGN - adjust;
  if (arena_len <= adjust) return MCDF_MICRO_E_ARENA;
  base = raw + adjust;
  usable = arena_len - adjust;

  header_bytes = mm_align_up(sizeof(struct mcdf_micro_reader));
  if (usable < header_bytes) return MCDF_MICRO_E_ARENA;

  r = (mcdf_micro_reader *)(void *)base;
  r->src = *src;
  r->base = base;
  r->len = usable;
  r->head = header_bytes;
  r->tail = usable;
  r->count = 0;
  r->entries = (struct mm_entry *)(void *)(base + header_bytes);

  for (pos = 0; pos + MM_BLOCK <= src->size; ) {
    unsigned char block[MM_BLOCK];
    struct mm_header h;
    mcdf_micro_status st;
    uint64_t data_off;

    if (src->read(src->ctx, pos, block, MM_BLOCK) != 0) return MCDF_MICRO_E_IO;

    st = mm_header_parse(block, &h);
    if (st != MCDF_MICRO_OK) return st;
    if (h.is_zero) { saw_end = 1; break; }

    data_off = pos + MM_BLOCK;
    /* The one check that keeps a lying size field from becoming an over-read.
     * Written as a subtraction because data_off + h.size could wrap. */
    if (h.size > src->size - data_off) return MCDF_MICRO_E_PARSE;

    /* Regular files only. Directories, links and the GNU/pax extension
     * entries are skipped exactly as the reference reader skips them; a
     * container that needs them to be understood is outside the spec's
     * "members ordered deterministically" interchange form. */
    if (h.typeflag == '0' || h.typeflag == '\0') {
      st = mm_push(r, &h, data_off);
      if (st != MCDF_MICRO_OK) return st;
    }

    /* Both operands are block multiples and h.size fits inside what remains,
     * so this lands on a block boundary at or before the end. */
    pos = data_off + mm_padded(h.size);
  }

  if (!saw_end) return MCDF_MICRO_E_PARSE;  /* ran out before the terminator */

  *out = r;
  return MCDF_MICRO_OK;
}

void mcdf_micro_close(mcdf_micro_reader *reader) {
  if (reader == NULL) return;
  /* The arena belongs to the caller; poison only the handle, so a use after
   * close fails on a NULL callback instead of walking a stale index. */
  reader->src.read = NULL;
  reader->src.ctx = NULL;
  reader->src.size = 0;
  reader->entries = NULL;
  reader->count = 0;
}

/* ---------------------------------------------------------------- members */

size_t mcdf_micro_count(mcdf_micro_reader *reader) {
  return reader == NULL ? 0u : reader->count;
}

static struct mm_entry *mm_find(mcdf_micro_reader *r, const char *path) {
  size_t i;
  for (i = 0; i < r->count; ++i) {
    if (mm_streq(r->entries[i].name, path)) return &r->entries[i];
  }
  return NULL;
}

mcdf_micro_status mcdf_micro_at(mcdf_micro_reader *reader, size_t index,
                                mcdf_micro_member_info *out) {
  if (reader == NULL || out == NULL) return MCDF_MICRO_E_INVAL;
  if (index >= reader->count) return MCDF_MICRO_E_NOT_FOUND;
  out->path = reader->entries[index].name;
  out->offset = reader->entries[index].data_off;
  out->size = reader->entries[index].size;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_member(mcdf_micro_reader *reader, const char *path,
                                    mcdf_micro_member_info *out) {
  struct mm_entry *e;
  if (reader == NULL || path == NULL || out == NULL) return MCDF_MICRO_E_INVAL;
  e = mm_find(reader, path);
  if (e == NULL) return MCDF_MICRO_E_NOT_FOUND;
  out->path = e->name;
  out->offset = e->data_off;
  out->size = e->size;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mcdf_micro_read_at(mcdf_micro_reader *reader, const char *path,
                                     uint64_t off, void *dst, size_t len) {
  struct mm_entry *e;
  if (reader == NULL || path == NULL) return MCDF_MICRO_E_INVAL;
  if (len != 0 && dst == NULL) return MCDF_MICRO_E_INVAL;
  if (reader->src.read == NULL) return MCDF_MICRO_E_INVAL;

  e = mm_find(reader, path);
  if (e == NULL) return MCDF_MICRO_E_NOT_FOUND;
  if (off > e->size || (uint64_t)len > e->size - off) return MCDF_MICRO_E_RANGE;
  if (len == 0) return MCDF_MICRO_OK;

  if (reader->src.read(reader->src.ctx, e->data_off + off, dst, len) != 0) {
    return MCDF_MICRO_E_IO;
  }
  return MCDF_MICRO_OK;
}

/* ----------------------------------------------------------------- cursor */

mcdf_micro_status mm_cursor_open(mcdf_micro_reader *r, const char *path,
                                 struct mm_cursor *c) {
  struct mm_entry *e;
  if (r == NULL || path == NULL || c == NULL) return MCDF_MICRO_E_INVAL;
  if (r->src.read == NULL) return MCDF_MICRO_E_INVAL;
  e = mm_find(r, path);
  if (e == NULL) return MCDF_MICRO_E_NOT_FOUND;
  c->r = r;
  c->base = e->data_off;
  c->size = e->size;
  c->pos = 0;
  c->have = 0;
  c->at = 0;
  return MCDF_MICRO_OK;
}

mcdf_micro_status mm_cursor_next(struct mm_cursor *c, unsigned char *ch) {
  if (c->pos >= c->size) return MCDF_MICRO_E_NOT_FOUND;  /* end of member */
  if (c->at >= c->have) {
    const uint64_t remain = c->size - c->pos;
    const size_t want = remain < (uint64_t)MM_WINDOW ? (size_t)remain : MM_WINDOW;
    if (c->r->src.read(c->r->src.ctx, c->base + c->pos, c->buf, want) != 0) {
      return MCDF_MICRO_E_IO;
    }
    c->have = want;
    c->at = 0;
  }
  *ch = c->buf[c->at];
  c->at += 1u;
  c->pos += 1u;
  return MCDF_MICRO_OK;
}
