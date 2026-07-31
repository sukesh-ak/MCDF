/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */
#ifndef MCDF_MICRO_INTERNAL_H
#define MCDF_MICRO_INTERNAL_H

/* Shared internals. Not installed, not part of the API: nothing here may leak
 * into a public header, or the C++ compile gate stops meaning anything. */

#include <stddef.h>
#include <stdint.h>

#include "mcdf_micro/mcdf_micro.h"

/* A compile-time assert that spells the same in C99 and C++, so the header
 * gate builds this file's neighbours without a dialect fork. */
#define MM_STATIC_ASSERT(cond, tag) \
  typedef char mm_static_assert_##tag[(cond) ? 1 : -1]

#define MM_BLOCK 512u

/* One indexed member. Kept to three fields on purpose: the arena budget in
 * MCDF_MICRO_ENTRY_SIZE is a published number that firmware sizes against. */
struct mm_entry {
  uint64_t    data_off;  /* absolute offset of the member's bytes */
  uint64_t    size;
  const char *name;      /* NUL-terminated, in the arena tail */
};

struct mcdf_micro_reader {
  mcdf_micro_source src;
  struct mm_entry  *entries;
  size_t            count;
  unsigned char    *base;      /* aligned arena base (this struct sits here) */
  size_t            len;       /* usable bytes from base */
  size_t            head;      /* bytes used from the front */
  size_t            tail;      /* offset of the lowest name byte */
};

/* ---- ustar.c ------------------------------------------------------------ */

/* Decoded USTAR header. `name` is assembled from the prefix and name fields
 * into caller storage, so nothing points back into the block buffer. */
struct mm_header {
  char     name[257];  /* 155 prefix + '/' + 100 name + NUL */
  size_t   name_len;
  uint64_t size;
  char     typeflag;
  int      is_zero;    /* end-of-archive marker block */
};

/* Parses one 512-byte header block. E_PARSE on a bad checksum, an unusable
 * size field, or a name that cannot be assembled. */
mcdf_micro_status mm_header_parse(const unsigned char *block,
                                  struct mm_header *out);

/* Rounds `size` up to a whole number of 512-byte blocks. `size` must already
 * have been bounds-checked against the container. */
uint64_t mm_padded(uint64_t size);

/* ---- cursor: a byte stream over one member, via a small window ---------- */

#define MM_WINDOW 64

struct mm_cursor {
  mcdf_micro_reader *r;
  uint64_t           base;   /* absolute offset of the member */
  uint64_t           size;   /* member length */
  uint64_t           pos;    /* next byte to hand out, relative to base */
  unsigned char      buf[MM_WINDOW];
  size_t             have;   /* bytes valid in buf */
  size_t             at;     /* next byte to consume from buf */
};

/* Positions a cursor over an existing member. E_NOT_FOUND if absent. */
mcdf_micro_status mm_cursor_open(mcdf_micro_reader *r, const char *path,
                                 struct mm_cursor *c);

/* Next byte. Returns OK and sets *ch, E_NOT_FOUND at end of member (a plain
 * end signal, not a defect), or E_IO. */
mcdf_micro_status mm_cursor_next(struct mm_cursor *c, unsigned char *ch);

/* ---- line-oriented YAML helpers (yaml_min.c) ---------------------------- */

/* Shared by every reader of the spec's small YAML shapes - metadata.yaml,
 * schema.yaml, encryption/policy.yaml. None of them needs a conforming YAML
 * parser, and all three need the same five primitives, so they live in one
 * place rather than being re-derived per file. */

int mm_is_space(char c);

/* Reads one line into `buf` (NUL-terminated), stripping a trailing CR.
 * Returns E_NOT_FOUND at end of member. `*truncated` is set when the line did
 * not fit; the rest of that line is consumed either way. */
mcdf_micro_status mm_line(struct mm_cursor *c, char *buf, size_t cap,
                          size_t *len, int *truncated);

/* Trims spaces and tabs from both ends, in place. */
void mm_trim(char **s, size_t *len);

/* Resolves a YAML scalar in place: strips one layer of quotes and the escapes
 * that can appear inside them, or cuts an unquoted value at a trailing
 * comment. Returns the resolved length. */
size_t mm_scalar(char *v, size_t len);

/* Splits "key: value" at the first colon followed by space or end-of-line.
 * Returns 0 if the line is not a mapping entry. */
int mm_split(char *line, size_t len, char **key, size_t *key_len, char **val,
             size_t *val_len);

/* ---- small helpers shared by the parsers ------------------------------- */

size_t mm_strlen(const char *s);
int    mm_streq(const char *a, const char *b);

/* Copies `len` bytes of `src` into `dst` with a NUL, reporting the true length
 * in *out_len whether or not it fit. E_RANGE when it did not. `dst` may be
 * NULL, which reports the length and copies nothing. */
mcdf_micro_status mm_emit(const char *src, size_t len,
                          char *dst, size_t dst_len, size_t *out_len);

#endif /* MCDF_MICRO_INTERNAL_H */
