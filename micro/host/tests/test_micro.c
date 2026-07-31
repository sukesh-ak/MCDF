/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The mcdf_micro host suite.
 *
 * Two kinds of input, on purpose:
 *   - real .mcdf archives the reference C++ runtime produced, so the claim
 *     "an independent implementation reads the same bytes" is checked against
 *     bytes nobody wrote for this test;
 *   - synthetic archives built here, for the malformed cases a conforming
 *     packer will never emit and which are exactly where an MCU reader gets
 *     an over-read.
 *
 * Plain C99 with no framework: the point of writing the library in C is that
 * it builds and links as C, and a C++ test harness would quietly stop
 * checking that. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcdf_micro/mcdf_micro.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond)                                                         \
  do {                                                                      \
    ++g_checks;                                                             \
    if (!(cond)) {                                                          \
      printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
      ++g_fail;                                                             \
    }                                                                       \
  } while (0)

#define CHECK_ST(expr, want)                                                \
  do {                                                                      \
    const mcdf_micro_status st_ = (expr);                                   \
    ++g_checks;                                                             \
    if (st_ != (want)) {                                                    \
      printf("  FAIL  %s:%d  %s -> %s, wanted %s\n", __FILE__, __LINE__,    \
             #expr, mcdf_micro_status_str(st_),                             \
             mcdf_micro_status_str(want));                                  \
      ++g_fail;                                                             \
    }                                                                       \
  } while (0)

static void section(const char *name) { printf("%s\n", name); }

/* ------------------------------------------------------------- sources */

/* The callback seam, in its simplest host form. */
static int file_read(void *ctx, uint64_t off, void *dst, size_t len) {
  FILE *fp = (FILE *)ctx;
  if (fseek(fp, (long)off, SEEK_SET) != 0) return -1;
  return fread(dst, 1, len, fp) == len ? 0 : -1;
}

static int mem_read(void *ctx, uint64_t off, void *dst, size_t len);

struct mem_source {
  const unsigned char *bytes;
  size_t               len;
};

static int mem_read(void *ctx, uint64_t off, void *dst, size_t len) {
  const struct mem_source *m = (const struct mem_source *)ctx;
  if (off > (uint64_t)m->len || (uint64_t)len > (uint64_t)m->len - off) return -1;
  memcpy(dst, m->bytes + (size_t)off, len);
  return 0;
}

static int failing_read(void *ctx, uint64_t off, void *dst, size_t len) {
  (void)ctx; (void)off; (void)dst; (void)len;
  return -1;
}

static long file_size(FILE *fp) {
  long n;
  if (fseek(fp, 0, SEEK_END) != 0) return -1;
  n = ftell(fp);
  rewind(fp);
  return n;
}

/* ------------------------------------------------- synthetic tar builder */

static unsigned char g_tar[32768];
static size_t g_tar_len;

static void tar_reset(void) {
  memset(g_tar, 0, sizeof g_tar);
  g_tar_len = 0;
}

static unsigned char *tar_block(void) {
  unsigned char *b = g_tar + g_tar_len;
  g_tar_len += 512;
  return b;
}

static void put_octal(unsigned char *field, size_t width, unsigned long value) {
  size_t i;
  field[width - 1] = '\0';
  for (i = width - 1; i > 0; --i) {
    field[i - 1] = (unsigned char)('0' + (value & 7u));
    value >>= 3;
  }
}

/* `declared_size` is what the header claims; `data`/`data_len` is what is
 * actually written. They differ only in the tests that need them to. */
static void tar_add_raw(const char *prefix, const char *name, char typeflag,
                        unsigned long declared_size, const void *data,
                        size_t data_len) {
  unsigned char *h = tar_block();
  unsigned long sum = 0;
  size_t i;

  memcpy(h + 0, name, strlen(name));
  put_octal(h + 100, 8, 0644);
  put_octal(h + 108, 8, 0);
  put_octal(h + 116, 8, 0);
  put_octal(h + 124, 12, declared_size);
  put_octal(h + 136, 12, 0);
  h[156] = (unsigned char)typeflag;
  memcpy(h + 257, "ustar", 5);
  h[263] = '0';
  h[264] = '0';
  if (prefix != NULL) memcpy(h + 345, prefix, strlen(prefix));

  memset(h + 148, ' ', 8);
  for (i = 0; i < 512; ++i) sum += h[i];
  sprintf((char *)h + 148, "%06lo", sum);
  h[154] = '\0';
  h[155] = ' ';

  if (data_len > 0) {
    const size_t blocks = (data_len + 511) / 512;
    unsigned char *d = g_tar + g_tar_len;
    memcpy(d, data, data_len);
    g_tar_len += blocks * 512;
  }
}

static void tar_add(const char *name, const char *text) {
  tar_add_raw(NULL, name, '0', (unsigned long)strlen(text), text, strlen(text));
}

static void tar_end(void) { g_tar_len += 1024; }

static void tar_source(mcdf_micro_source *src, struct mem_source *m) {
  m->bytes = g_tar;
  m->len = g_tar_len;
  src->ctx = m;
  src->read = mem_read;
  src->size = (uint64_t)g_tar_len;
}

/* ------------------------------------------------------------ fixtures */

#define ARENA_BYTES MCDF_MICRO_ARENA_SIZE(64, 4096)
static unsigned char g_arena[ARENA_BYTES];

struct fixture {
  FILE              *fp;
  mcdf_micro_source  src;
  mcdf_micro_reader *r;
};

static int fixture_open(struct fixture *f, const char *path) {
  long n;
  f->fp = fopen(path, "rb");
  if (f->fp == NULL) {
    printf("  FAIL  cannot open fixture %s\n", path);
    ++g_fail;
    return 0;
  }
  n = file_size(f->fp);
  f->src.ctx = f->fp;
  f->src.read = file_read;
  f->src.size = (uint64_t)n;
  if (mcdf_micro_open(&f->src, g_arena, sizeof g_arena, &f->r) != MCDF_MICRO_OK) {
    printf("  FAIL  cannot index fixture %s\n", path);
    ++g_fail;
    fclose(f->fp);
    return 0;
  }
  return 1;
}

static void fixture_close(struct fixture *f) {
  mcdf_micro_close(f->r);
  fclose(f->fp);
}

/* Reads a whole member into `dst`; returns the status of the read. */
static mcdf_micro_status slurp(mcdf_micro_reader *r, const char *path, char *dst,
                               size_t cap) {
  mcdf_micro_member_info info;
  mcdf_micro_status st = mcdf_micro_member(r, path, &info);
  if (st != MCDF_MICRO_OK) return st;
  if (info.size + 1 > (uint64_t)cap) return MCDF_MICRO_E_RANGE;
  st = mcdf_micro_read_at(r, path, 0, dst, (size_t)info.size);
  if (st == MCDF_MICRO_OK) dst[info.size] = '\0';
  return st;
}

/* --------------------------------------------------------------- tests */

static void test_minimal(void) {
  struct fixture f;
  mcdf_micro_member_info info;
  char buf[512];
  size_t len = 0;

  section("minimal.mcdf (reference runtime output)");
  if (!fixture_open(&f, MCDF_MICRO_FIXTURE_MINIMAL)) return;

  CHECK(mcdf_micro_count(f.r) == 4);

  /* Archive order is the packer's: members sorted by path. */
  CHECK_ST(mcdf_micro_at(f.r, 0, &info), MCDF_MICRO_OK);
  CHECK(strcmp(info.path, "content.md") == 0);
  CHECK(info.size == 49);
  CHECK(info.offset == 512); /* first header, then data */

  CHECK_ST(mcdf_micro_at(f.r, 1, &info), MCDF_MICRO_OK);
  CHECK(strcmp(info.path, "manifest.json") == 0);
  CHECK(info.size == 303);

  CHECK_ST(mcdf_micro_at(f.r, 2, &info), MCDF_MICRO_OK);
  CHECK(strcmp(info.path, "metadata.yaml") == 0);
  CHECK(info.size == 52);

  CHECK_ST(mcdf_micro_at(f.r, 3, &info), MCDF_MICRO_OK);
  CHECK(strcmp(info.path, "schema.yaml") == 0);
  CHECK(info.size == 67);

  CHECK_ST(mcdf_micro_at(f.r, 4, &info), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_member(f.r, "nope.md", &info), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_member(f.r, "content", &info), MCDF_MICRO_E_NOT_FOUND);

  /* Whole member, then a window inside it. */
  CHECK_ST(slurp(f.r, "content.md", buf, sizeof buf), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "# Overview {#overview}\n\nA minimal MCDF document.\n") == 0);

  memset(buf, 0, sizeof buf);
  CHECK_ST(mcdf_micro_read_at(f.r, "content.md", 2, buf, 8), MCDF_MICRO_OK);
  buf[8] = '\0';
  CHECK(strcmp(buf, "Overview") == 0);

  /* A window that runs past the member is refused whole, never partially. */
  CHECK_ST(mcdf_micro_read_at(f.r, "content.md", 45, buf, 8), MCDF_MICRO_E_RANGE);
  CHECK_ST(mcdf_micro_read_at(f.r, "content.md", 50, buf, 1), MCDF_MICRO_E_RANGE);
  CHECK_ST(mcdf_micro_read_at(f.r, "content.md", 49, buf, 0), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_read_at(f.r, "content.md", 0, buf, 49), MCDF_MICRO_OK);

  /* metadata.yaml, unquoted scalars. */
  CHECK_ST(mcdf_micro_meta(f.r, "title", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "Minimal Document") == 0);
  CHECK(len == 16);
  CHECK_ST(mcdf_micro_meta(f.r, "version", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "1.0.0") == 0);
  CHECK_ST(mcdf_micro_meta(f.r, "language", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "en") == 0);
  CHECK_ST(mcdf_micro_meta(f.r, "author", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_meta_author(f.r, 0, buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);

  /* A value too large for the destination reports the size it needed. */
  len = 0;
  CHECK_ST(mcdf_micro_meta(f.r, "title", buf, 4, &len), MCDF_MICRO_E_RANGE);
  CHECK(len == 16);

  /* manifest.json, canonical (no whitespace). */
  CHECK_ST(mcdf_micro_manifest_field(f.r, "hash_algorithm", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "sha256") == 0);
  CHECK_ST(mcdf_micro_manifest_field(f.r, "mcdf_version", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "1.0") == 0);
  CHECK_ST(mcdf_micro_manifest_field(f.r, "nope", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);

  CHECK_ST(mcdf_micro_manifest_hash(f.r, "content.md", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf,
               "35fa5258bef385b260fd63be6fe1681c36ac6cf1fa80d15c357b0382e9f93e66") == 0);
  CHECK(len == 64);
  CHECK_ST(mcdf_micro_manifest_hash(f.r, "manifest.json", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND); /* the manifest never lists itself */

  len = 0;
  CHECK_ST(mcdf_micro_manifest_count(f.r, &len), MCDF_MICRO_OK);
  CHECK(len == 3);

  fixture_close(&f);
}

static void test_showcase(void) {
  struct fixture f;
  mcdf_micro_member_info info;
  char buf[256];
  char hash[128];
  size_t len = 0, hash_len = 0, i, total;

  section("showcase.mcdf (nested paths, quoted YAML, an author list)");
  if (!fixture_open(&f, MCDF_MICRO_FIXTURE_SHOWCASE)) return;

  CHECK(mcdf_micro_count(f.r) == 8);

  /* A path that came from the ustar prefix/name pair reassembles whole. */
  CHECK_ST(mcdf_micro_member(f.r, "assets/mcdf-logo.png", &info), MCDF_MICRO_OK);
  CHECK(info.size == 65360);
  CHECK_ST(mcdf_micro_member(f.r, "signatures/mcdf-project.sig", &info),
           MCDF_MICRO_OK);
  CHECK(info.size == 195);
  CHECK_ST(mcdf_micro_member(f.r, "content.md", &info), MCDF_MICRO_OK);
  CHECK(info.size == 3576);

  /* Every indexed path is one a caller could safely extract. */
  total = mcdf_micro_count(f.r);
  for (i = 0; i < total; ++i) {
    CHECK_ST(mcdf_micro_at(f.r, i, &info), MCDF_MICRO_OK);
    CHECK(mcdf_micro_path_is_safe(info.path));
  }

  /* A PNG read from the middle: offsets survive the walk. */
  CHECK_ST(mcdf_micro_read_at(f.r, "assets/mcdf-logo.png", 0, buf, 8),
           MCDF_MICRO_OK);
  CHECK((unsigned char)buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' &&
        buf[3] == 'G');
  CHECK_ST(mcdf_micro_read_at(f.r, "assets/mcdf-logo.png", 65359, buf, 1),
           MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_read_at(f.r, "assets/mcdf-logo.png", 65359, buf, 2),
           MCDF_MICRO_E_RANGE);

  /* Quoted scalars lose their quotes; an unquoted one keeps its shape. */
  CHECK_ST(mcdf_micro_meta(f.r, "title", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "The MCDF Showcase") == 0);
  CHECK_ST(mcdf_micro_meta(f.r, "classification", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "public") == 0);
  CHECK_ST(mcdf_micro_meta(f.r, "created_at", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "2026-07-22T00:00:00Z") == 0);

  /* An author name lives one level inside a block sequence, and `id:` on the
   * sibling line must not be mistaken for it. */
  CHECK_ST(mcdf_micro_meta_author(f.r, 0, buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "The MCDF Project") == 0);
  CHECK(len == 16);
  CHECK_ST(mcdf_micro_meta_author(f.r, 1, buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);
  /* `name` is not a top-level key just because it appears in the file. */
  CHECK_ST(mcdf_micro_meta(f.r, "name", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_meta(f.r, "id", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);

  /* The manifest lists four of the eight members: signatures and the audit
   * files are excluded by conformance/errors.md, and it never lists itself. */
  CHECK_ST(mcdf_micro_manifest_count(f.r, &len), MCDF_MICRO_OK);
  CHECK(len == 4);
  CHECK_ST(mcdf_micro_manifest_at(f.r, 0, buf, sizeof buf, &len, hash,
                                  sizeof hash, &hash_len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "assets/mcdf-logo.png") == 0);
  CHECK(hash_len == 64);
  CHECK(strcmp(hash,
               "6a53b0bb7495eea96785f73c382a2ccb314677d6266d5a6197d6b6c6cc01d0ba") == 0);
  CHECK_ST(mcdf_micro_manifest_at(f.r, 4, buf, sizeof buf, &len, hash,
                                  sizeof hash, &hash_len),
           MCDF_MICRO_E_NOT_FOUND);
  /* Skipping a destination still reports the length. */
  CHECK_ST(mcdf_micro_manifest_at(f.r, 1, NULL, 0, &len, hash, sizeof hash,
                                  &hash_len),
           MCDF_MICRO_OK);
  CHECK(len == 10); /* "content.md" */

  /* Every listed file is present in the container - the shape the Integrity
   * check will take once it can hash. */
  for (i = 0; i < 4; ++i) {
    CHECK_ST(mcdf_micro_manifest_at(f.r, i, buf, sizeof buf, &len, NULL, 0, NULL),
             MCDF_MICRO_OK);
    CHECK_ST(mcdf_micro_member(f.r, buf, &info), MCDF_MICRO_OK);
  }

  fixture_close(&f);
}

static void test_malformed(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  static const unsigned char kEmpty[1024] = {0};

  section("malformed and hostile archives");

  /* Two zero blocks: a valid, empty archive. */
  mem.bytes = kEmpty;
  mem.len = sizeof kEmpty;
  src.ctx = &mem;
  src.read = mem_read;
  src.size = (uint64_t)sizeof kEmpty;
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 0);

  /* Too short to be an archive at all. */
  src.size = 512;
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* Not a whole number of blocks: the cheapest truncation to detect. */
  src.size = 1025;
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* A size field that reaches past the end of the container. This is the
   * over-read the library exists to refuse. */
  tar_reset();
  tar_add_raw(NULL, "content.md", '0', 100000UL, "short", 5);
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* A corrupted header no longer sums to its stored checksum. */
  tar_reset();
  tar_add("content.md", "hello");
  tar_end();
  g_tar[3] = 'X';
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* No end-of-archive marker: the data simply stops. */
  tar_reset();
  tar_add("content.md", "hello");
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* A non-octal size field is refused rather than guessed at. */
  tar_reset();
  tar_add("content.md", "hello");
  tar_end();
  memcpy(g_tar + 124, "zzzzzzzzzzz", 11);
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* An empty name field. */
  tar_reset();
  tar_add("content.md", "hello");
  tar_end();
  memset(g_tar, 0, 100);
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* A magic that is not ustar is another format wearing a tar header. */
  tar_reset();
  tar_add("content.md", "hello");
  tar_end();
  memcpy(g_tar + 257, "zip\0\0", 5);
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_PARSE);

  /* An I/O failure at the very first block is reported as one. */
  src.read = failing_read;
  src.size = 4096;
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_IO);

  /* Arguments. */
  CHECK_ST(mcdf_micro_open(NULL, g_arena, sizeof g_arena, &r), MCDF_MICRO_E_INVAL);
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, NULL, 0, &r), MCDF_MICRO_E_INVAL);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, NULL), MCDF_MICRO_E_INVAL);
}

static void test_structure(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  mcdf_micro_member_info info;
  /* 155 + 100 is the widest path ustar can carry; this one needs the split. */
  static const char kPrefix[] =
      "a/very/long/directory/chain/that/comfortably/exceeds/the/hundred/byte/"
      "name/field/and/therefore/has/to/use/the/prefix";
  char joined[256];

  section("archive structure");

  /* Prefix + name reassembly. */
  tar_reset();
  tar_add_raw(kPrefix, "leaf.txt", '0', 5, "hello", 5);
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 1);
  sprintf(joined, "%s/leaf.txt", kPrefix);
  CHECK_ST(mcdf_micro_member(r, joined, &info), MCDF_MICRO_OK);
  CHECK(info.size == 5);

  /* Directory entries carry no bytes and are not members. */
  tar_reset();
  tar_add_raw(NULL, "assets/", '5', 0, NULL, 0);
  tar_add("assets/a.txt", "A");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 1);
  CHECK_ST(mcdf_micro_member(r, "assets/a.txt", &info), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_member(r, "assets/", &info), MCDF_MICRO_E_NOT_FOUND);

  /* A zero-length member is a member. */
  tar_reset();
  tar_add("empty.txt", "");
  tar_add("content.md", "x");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 2);
  CHECK_ST(mcdf_micro_member(r, "empty.txt", &info), MCDF_MICRO_OK);
  CHECK(info.size == 0);
  CHECK_ST(mcdf_micro_read_at(r, "empty.txt", 0, joined, 0), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_read_at(r, "empty.txt", 0, joined, 1), MCDF_MICRO_E_RANGE);
}

static void test_arena(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  unsigned char small[MCDF_MICRO_ARENA_SIZE(1, 16)];
  unsigned char tiny[16];
  unsigned char slab[ARENA_BYTES + 8];

  section("arena budget");

  tar_reset();
  tar_add("content.md", "a");
  tar_add("metadata.yaml", "title: T\n");
  tar_add("schema.yaml", "sections: []\n");
  tar_end();
  tar_source(&src, &mem);

  /* Room for one member, three in the archive: a clean refusal, and never a
   * partially built index handed back. */
  r = NULL;
  CHECK_ST(mcdf_micro_open(&src, small, sizeof small, &r), MCDF_MICRO_E_ARENA);
  CHECK(r == NULL);

  /* Too small even for the reader's own header. */
  CHECK_ST(mcdf_micro_open(&src, tiny, sizeof tiny, &r), MCDF_MICRO_E_ARENA);

  /* The published macro is an upper bound: sizing from it always works, and
   * an arena the caller did not align is realigned rather than refused. */
  CHECK_ST(mcdf_micro_open(&src, slab, sizeof slab, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 3);
  CHECK_ST(mcdf_micro_open(&src, slab + 1, sizeof slab - 1, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 3);
  CHECK_ST(mcdf_micro_open(&src, slab + 3, sizeof slab - 3, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_count(r) == 3);

  /* After close the handle is inert rather than dangling. */
  mcdf_micro_close(r);
  CHECK(mcdf_micro_count(r) == 0);
  mcdf_micro_close(NULL);
}

static void test_yaml_shapes(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  char buf[256];
  size_t len = 0;

  section("metadata.yaml shapes");

  tar_reset();
  tar_add("metadata.yaml",
          "---\n"
          "# a comment line\n"
          "title: 'It''s quoted'   # trailing comment\n"
          "anchor: Overview{#overview}\n"
          "commented: plain value # and a comment\n"
          "escaped: \"tab\\there\"\n"
          "empty:\n"
          "authors:\n"
          "  - name: First Author\n"
          "    id: did:key:zAbc\n"
          "  -\n"
          "    name: \"Second Author\"\n"
          "  - Third Author\n"
          "language: en\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);

  CHECK_ST(mcdf_micro_meta(r, "title", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "It's quoted") == 0);

  /* YAML starts a comment at a '#' preceded by whitespace, and only there.
   * One with no space before it belongs to the value - which is what keeps a
   * heading anchor like {#overview} intact. */
  CHECK_ST(mcdf_micro_meta(r, "anchor", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "Overview{#overview}") == 0);
  CHECK_ST(mcdf_micro_meta(r, "commented", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "plain value") == 0);

  CHECK_ST(mcdf_micro_meta(r, "escaped", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "tab\there") == 0);

  CHECK_ST(mcdf_micro_meta(r, "empty", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(len == 0);
  CHECK(buf[0] == '\0');

  /* A key that only exists after the authors block is still top-level. */
  CHECK_ST(mcdf_micro_meta(r, "language", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "en") == 0);

  CHECK_ST(mcdf_micro_meta_author(r, 0, buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "First Author") == 0);
  CHECK_ST(mcdf_micro_meta_author(r, 1, buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "Second Author") == 0);
  CHECK_ST(mcdf_micro_meta_author(r, 2, buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(strcmp(buf, "Third Author") == 0);
  CHECK_ST(mcdf_micro_meta_author(r, 3, buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);

  /* An empty flow sequence is an answer, not the start of a block. */
  tar_reset();
  tar_add("metadata.yaml", "title: T\nauthors: []\ncreated_at: \"\"\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_meta_author(r, 0, buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_meta(r, "created_at", buf, sizeof buf, &len), MCDF_MICRO_OK);
  CHECK(len == 0);

  /* No metadata.yaml at all. */
  tar_reset();
  tar_add("content.md", "x");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_meta(r, "title", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);
}

static void test_manifest_shapes(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  char buf[256];
  size_t len = 0;

  section("manifest.json shapes");

  /* Pretty-printed, with escapes and a nested value to step over. A container
   * assembled by hand in a directory is a first-class workflow, so the
   * whitespace-free canonical form cannot be the only one that parses. */
  tar_reset();
  tar_add("manifest.json",
          "{\n"
          "  \"mcdf_version\": \"1.0\",\n"
          "  \"extra\": { \"nested\": [1, 2, {\"deep\": true}], \"n\": null },\n"
          "  \"files\": {\n"
          "    \"assets/a \\u0041 b.png\": \"aa\",\n"
          "    \"content.md\": \"bb\",\n"
          "    \"quote\\\"path.md\": \"cc\"\n"
          "  },\n"
          "  \"hash_algorithm\": \"sha256\"\n"
          "}\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);

  CHECK_ST(mcdf_micro_manifest_field(r, "mcdf_version", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "1.0") == 0);
  CHECK_ST(mcdf_micro_manifest_field(r, "hash_algorithm", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "sha256") == 0);

  CHECK_ST(mcdf_micro_manifest_count(r, &len), MCDF_MICRO_OK);
  CHECK(len == 3);

  CHECK_ST(mcdf_micro_manifest_hash(r, "assets/a A b.png", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "aa") == 0);
  CHECK_ST(mcdf_micro_manifest_hash(r, "quote\"path.md", buf, sizeof buf, &len),
           MCDF_MICRO_OK);
  CHECK(strcmp(buf, "cc") == 0);
  CHECK_ST(mcdf_micro_manifest_hash(r, "missing.md", buf, sizeof buf, &len),
           MCDF_MICRO_E_NOT_FOUND);

  /* Truncated JSON is a parse error, not a silent empty answer. */
  tar_reset();
  tar_add("manifest.json", "{\"files\":{\"a\":\"b\"");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_manifest_count(r, &len), MCDF_MICRO_E_PARSE);

  tar_reset();
  tar_add("manifest.json", "not json at all");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_manifest_field(r, "mcdf_version", buf, sizeof buf, &len),
           MCDF_MICRO_E_PARSE);

  /* A manifest with no files object. */
  tar_reset();
  tar_add("manifest.json", "{\"mcdf_version\":\"1.0\"}");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_manifest_count(r, &len), MCDF_MICRO_E_NOT_FOUND);

  /* No manifest.json member at all. */
  tar_reset();
  tar_add("content.md", "x");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_manifest_count(r, &len), MCDF_MICRO_E_NOT_FOUND);
}

static void test_path_safety(void) {
  section("path safety (the kit's E_ASSET_PATH_ESCAPE rule)");

  CHECK(mcdf_micro_path_is_safe("content.md"));
  CHECK(mcdf_micro_path_is_safe("assets/logo.png"));
  CHECK(mcdf_micro_path_is_safe("a/b/c/d.txt"));
  CHECK(mcdf_micro_path_is_safe("..hidden"));   /* not a ".." component */
  CHECK(mcdf_micro_path_is_safe("a..b/c"));
  CHECK(mcdf_micro_path_is_safe("./a.txt"));    /* "." goes nowhere */

  CHECK(!mcdf_micro_path_is_safe("/etc/passwd"));
  CHECK(!mcdf_micro_path_is_safe("../escape.md"));
  CHECK(!mcdf_micro_path_is_safe("assets/../../escape.md"));
  CHECK(!mcdf_micro_path_is_safe("assets/.."));
  CHECK(!mcdf_micro_path_is_safe("C:/windows/system32"));
  CHECK(!mcdf_micro_path_is_safe("assets\\logo.png"));
  CHECK(!mcdf_micro_path_is_safe("a//b"));
  CHECK(!mcdf_micro_path_is_safe("a/"));
  CHECK(!mcdf_micro_path_is_safe(""));
  CHECK(!mcdf_micro_path_is_safe(NULL));
}

static void test_reporting(void) {
  section("what this build says it is");

  /* This build is the container reader: Core and nothing else. A build must never
   * claim a profile whose code is not compiled in. */
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_CORE) != 0);
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_INTEGRITY) == 0);
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_SIGNED) == 0);
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_RENDER) == 0);

  /* Kit codes keep the kit's spelling, so a harness can print them straight. */
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_OK), "OK") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_MISSING_CONTENT),
               "E_MISSING_CONTENT") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_ASSET_PATH_ESCAPE),
               "E_ASSET_PATH_ESCAPE") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_UNIMPLEMENTED),
               "E_UNIMPLEMENTED") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_DISABLED), "E_DISABLED") == 0);
}

int main(void) {
  printf("mcdf_micro host tests\n\n");

  test_minimal();
  test_showcase();
  test_malformed();
  test_structure();
  test_arena();
  test_yaml_shapes();
  test_manifest_shapes();
  test_path_safety();
  test_reporting();

  printf("\n-------------------------------------\n");
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}
