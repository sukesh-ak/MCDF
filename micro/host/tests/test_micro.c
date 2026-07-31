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
#include "mcdf_micro/mcdf_micro_render.h"
#include "mcdf_micro/mcdf_micro_verify.h"

/* A published conformance vector, in the one serialization this reader has.
 * Scoring against the kit is the CLI's job; these use the same containers to
 * check the library underneath it, so a failure says which of the two broke. */
#define VECTOR(name) MCDF_MICRO_VECTORS "/" name "/container.mcdf"

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

/* ------------------------------------------------------------ issue log */

/* The validation entry points report through a callback rather than a list,
 * because the core never allocates. A test needs the list, so it builds one. */
struct issue_log {
  mcdf_micro_status codes[16];
  char              details[16][160];
  size_t            n;
};

static void log_issue(void *ctx, mcdf_micro_status code, const char *detail) {
  struct issue_log *log = (struct issue_log *)ctx;
  if (log->n < sizeof log->codes / sizeof log->codes[0]) {
    log->codes[log->n] = code;
    strncpy(log->details[log->n], detail, sizeof log->details[0] - 1);
    log->details[log->n][sizeof log->details[0] - 1] = '\0';
  }
  ++log->n;
}

static int logged(const struct issue_log *log, mcdf_micro_status code,
                  const char *detail) {
  size_t i;
  for (i = 0; i < log->n; ++i) {
    if (log->codes[i] == code && strcmp(log->details[i], detail) == 0) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------- schema.yaml */

static void test_schema(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  char id[MCDF_MICRO_ID_MAX];
  size_t count = 0, len = 0;
  int required = 0;

  section("schema.yaml sections");

  tar_reset();
  tar_add("schema.yaml",
          "document_type: contract\n"
          "sections:\n"
          "  - id: overview\n"
          "    title: Overview\n"
          "  - id: terms\n"
          "    title: Terms and Conditions\n"
          "    required: true\n"
          "  - title: a section that declares no id\n"
          "  - id: appendix\n"
          "    required: \"yes\"\n"
          "version: 1\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);

  CHECK_ST(mcdf_micro_schema_count(r, &count), MCDF_MICRO_OK);
  CHECK(count == 4);

  CHECK_ST(mcdf_micro_schema_at(r, 0, id, sizeof id, &len, &required),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "overview") == 0 && required == 0 && len == 8);

  CHECK_ST(mcdf_micro_schema_at(r, 1, id, sizeof id, &len, &required),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "terms") == 0 && required == 1);

  /* A section with no id is still a section: counted, reported as the empty id
   * that binds to nothing. Skipping it would renumber every one after it. */
  CHECK_ST(mcdf_micro_schema_at(r, 2, id, sizeof id, &len, &required),
           MCDF_MICRO_OK);
  CHECK(len == 0 && required == 0);

  /* `required: "yes"` is true, matching what the reference's YAML library
   * resolves - the two implementations have to agree on which sections a
   * document says are mandatory. */
  CHECK_ST(mcdf_micro_schema_at(r, 3, id, sizeof id, &len, &required),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "appendix") == 0 && required == 1);

  CHECK_ST(mcdf_micro_schema_at(r, 4, id, sizeof id, &len, &required),
           MCDF_MICRO_E_NOT_FOUND);

  /* An empty flow sequence is a complete answer: no sections. */
  tar_reset();
  tar_add("schema.yaml", "document_type: note\nsections: []\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_schema_count(r, &count), MCDF_MICRO_OK);
  CHECK(count == 0);

  /* No schema at all is not a defect - a document may make no structural
   * claims - so it is distinguishable from an empty one. */
  tar_reset();
  tar_add("content.md", "# x\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_schema_count(r, &count), MCDF_MICRO_E_NOT_FOUND);
}

/* ---------------------------------------------------------- heading anchors */

static void test_anchors(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  char id[MCDF_MICRO_ID_MAX];
  char content[2048];
  size_t count = 0, len = 0, i;
  int level = 0;

  section("content.md heading anchors");

  tar_reset();
  tar_add("content.md",
          "# Overview {#overview}\n"
          "\n"
          "A paragraph mentioning a # hash and a {#brace} that is not one.\n"
          "\n"
          "## Terms and Conditions {#terms} ##\n"
          "\n"
          "```\n"
          "# fenced code {#fenced}\n"
          "```\n"
          "\n"
          "Setext Title {#setext}\n"
          "======================\n"
          "\n"
          "Second Level {#second}\n"
          "---\n"
          "\n"
          "    # indented code {#indented}\n"
          "\n"
          "\t# tab-indented code {#tabbed}\n"
          "\n"
          "#no-space {#nospace}\n"
          "\n"
          "####### seven hashes {#seven}\n"
          "\n"
          "### Empty anchor {#}\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);

  CHECK_ST(mcdf_micro_anchor_count(r, &count), MCDF_MICRO_OK);
  CHECK(count == 4);

  CHECK_ST(mcdf_micro_anchor_at(r, 0, id, sizeof id, &len, &level),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "overview") == 0 && level == 1);

  /* The ATX closing sequence is not part of the heading text, so the anchor is
   * still the last thing in it. */
  CHECK_ST(mcdf_micro_anchor_at(r, 1, id, sizeof id, &len, &level),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "terms") == 0 && level == 2);

  CHECK_ST(mcdf_micro_anchor_at(r, 2, id, sizeof id, &len, &level),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "setext") == 0 && level == 1);

  CHECK_ST(mcdf_micro_anchor_at(r, 3, id, sizeof id, &len, &level),
           MCDF_MICRO_OK);
  CHECK(strcmp(id, "second") == 0 && level == 2);

  CHECK_ST(mcdf_micro_anchor_at(r, 4, id, sizeof id, &len, &level),
           MCDF_MICRO_E_NOT_FOUND);

  CHECK_ST(mcdf_micro_has_anchor(r, "overview"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "second"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "fenced"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "indented"), MCDF_MICRO_E_NOT_FOUND);
  /* A tab is four columns, so this is indented code too. Counting it as one
   * byte of indent would make it a heading here and code in the reference -
   * two implementations disagreeing about whether a document is valid. */
  CHECK_ST(mcdf_micro_has_anchor(r, "tabbed"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "nospace"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "seven"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "brace"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "over"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, ""), MCDF_MICRO_E_NOT_FOUND);

  /* A setext heading whose text opens with a character that also opens a
   * block. Excluding these by first character alone lost real headings, and
   * under spec 4.2 a lost heading means failing a document that another
   * implementation accepts. */
  tar_reset();
  tar_add("content.md",
          "*Emphasised Title* {#emph}\n"
          "==========================\n"
          "\n"
          "`code` in the title {#tick}\n"
          "---------------------------\n"
          "\n"
          "=leading equals {#eq}\n"
          "---------------------\n"
          "\n"
          "-not-a-bullet {#dash}\n"
          "---------------------\n"
          "\n"
          /* No space after the dot, so this is prose, not a list marker. */
          "7.not-an-ordered-marker {#seven-dot}\n"
          "------------------------------------\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "emph"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "tick"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "eq"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "dash"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "seven-dot"), MCDF_MICRO_OK);

  /* ...but a line that really does open another block is still not setext
   * text, so the underline below it forms no heading. */
  tar_reset();
  tar_add("content.md",
          "- a bullet {#bullet}\n"
          "--------------------\n"
          "\n"
          "> quoted {#quoted}\n"
          "------------------\n"
          "\n"
          "# atx already {#atx}\n"
          "--------------------\n"
          "\n"
          "```\n"
          "fenced {#fenced}\n"
          "----------------\n"
          "```\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "bullet"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "quoted"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "fenced"), MCDF_MICRO_E_NOT_FOUND);
  /* The ATX heading binds on its own line; the dashes below it are a thematic
   * break, not a second heading over the same text. */
  CHECK_ST(mcdf_micro_has_anchor(r, "atx"), MCDF_MICRO_OK);

  /* Spec 4.2: only a top-level heading binds. A nested one is a heading quoted
   * inside another block, and admitting it would need a CommonMark parser -
   * which is the whole reason the rule says top level. */
  tar_reset();
  tar_add("content.md",
          "# Overview {#overview}\n"
          "\n"
          "> ## Quoted {#quoted}\n"
          "\n"
          "- ### In a list item {#item}\n"
          "\n"
          "#### Back at the top {#back}\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "overview"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "quoted"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "item"), MCDF_MICRO_E_NOT_FOUND);
  CHECK_ST(mcdf_micro_has_anchor(r, "back"), MCDF_MICRO_OK);

  /* CRLF, because a document authored on Windows is the same document. */
  tar_reset();
  tar_add("content.md",
          "# Overview {#overview}\r\n"
          "\r\n"
          "Setext {#crlf}\r\n"
          "======\r\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "overview"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "crlf"), MCDF_MICRO_OK);

  /* A heading far longer than any line buffer. The anchor is at the end, so
   * the scanner keeps a rolling tail rather than a prefix and a verbose
   * document is not a rejected one. */
  memcpy(content, "# ", 2);
  for (i = 2; i < 1500; ++i) content[i] = 'x';
  memcpy(content + 1500, " {#tail}\n", 10);
  tar_reset();
  tar_add("content.md", content);
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "tail"), MCDF_MICRO_OK);

  /* An id longer than the reader will match is refused, not truncated to a
   * prefix that would bind the wrong section. */
  memcpy(content, "# T {#", 6);
  for (i = 6; i < 6 + MCDF_MICRO_ID_MAX + 10; ++i) content[i] = 'a';
  memcpy(content + 6 + MCDF_MICRO_ID_MAX + 10, "}\n", 3);
  tar_reset();
  tar_add("content.md", content);
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_anchor_at(r, 0, id, sizeof id, &len, NULL),
           MCDF_MICRO_E_RANGE);
  CHECK(len == MCDF_MICRO_ID_MAX + 10);

  /* No content.md: nothing to scan, and that is not a parse failure. */
  tar_reset();
  tar_add("metadata.yaml", "title: x\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "overview"), MCDF_MICRO_E_NOT_FOUND);
}

/* ---------------------------------------------------------------- sealed */

static void test_sealed(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;

  section("sealed members (spec 5.2)");

  tar_reset();
  tar_add("content.md", "ciphertext");
  tar_add("encryption/policy.yaml",
          "method: aes-256-gcm\n"
          "key_management: hpke\n"
          "encrypted_files:\n"
          "  - content.md\n"
          "recipients:\n"
          "  - id: did:key:z6LS\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_is_sealed(r, "content.md") != 0);
  CHECK(mcdf_micro_is_sealed(r, "schema.yaml") == 0);

  tar_reset();
  tar_add("content.md", "ciphertext");
  tar_add("encryption/policy.yaml",
          "encrypted_files: [content.md, assets/a.png]\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_is_sealed(r, "content.md") != 0);
  CHECK(mcdf_micro_is_sealed(r, "assets/a.png") != 0);
  CHECK(mcdf_micro_is_sealed(r, "metadata.yaml") == 0);

  /* No policy, nothing sealed. */
  tar_reset();
  tar_add("content.md", "# x {#x}\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_is_sealed(r, "content.md") == 0);

  /* A policy this reader cannot finish reading does not earn the member a
   * "plaintext" verdict - "not sealed" is a claim, and it has no basis for it. */
  tar_reset();
  tar_add("content.md", "ciphertext");
  tar_add("encryption/policy.yaml", "encrypted_files:\n  - \001\002 not\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK(mcdf_micro_is_sealed(r, "content.md") == 0); /* readable, just unlisted */
}

/* ------------------------------------------------------- Core validation */

static void test_validate_core(void) {
  struct fixture f;
  struct issue_log log;
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;

  section("validate: the Core profile");

  /* The kit's own vectors, so this checks the same containers the harness
   * scores rather than a paraphrase of them. */
  if (fixture_open(&f, VECTOR("valid/minimal"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_validate_core(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 0);
    fixture_close(&f);
  }

  if (fixture_open(&f, VECTOR("invalid/missing-content"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_validate_core(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 1);
    CHECK(logged(&log, MCDF_MICRO_E_MISSING_CONTENT, "content.md"));
    fixture_close(&f);
  }

  if (fixture_open(&f, VECTOR("invalid/required-section-missing"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_validate_core(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 1);
    CHECK(logged(&log, MCDF_MICRO_E_REQUIRED_SECTION_MISSING, "terms"));
    fixture_close(&f);
  }

  /* A section that is declared but not marked required is unbound, not
   * missing: the same document, two different verdicts, and the kit's error
   * taxonomy distinguishes them. */
  tar_reset();
  tar_add("content.md", "# Overview {#overview}\n");
  tar_add("schema.yaml",
          "sections:\n"
          "  - id: overview\n"
          "  - id: optional-extra\n"
          "  - id: mandatory\n"
          "    required: true\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  log.n = 0;
  CHECK_ST(mcdf_micro_validate_core(r, log_issue, &log, NULL), MCDF_MICRO_OK);
  CHECK(log.n == 2);
  CHECK(logged(&log, MCDF_MICRO_E_SCHEMA_UNBOUND, "optional-extra"));
  CHECK(logged(&log, MCDF_MICRO_E_REQUIRED_SECTION_MISSING, "mandatory"));

  /* No schema: no structural claims, nothing to check. */
  tar_reset();
  tar_add("content.md", "just prose\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  log.n = 0;
  CHECK_ST(mcdf_micro_validate_core(r, log_issue, &log, NULL), MCDF_MICRO_OK);
  CHECK(log.n == 0);

  /* Sealed content: the headings are unreadable, so this reader says so once
   * rather than reporting every section as missing. Blaming the document for
   * an implementation's gap is the wrong answer, and the kit names it as such. */
  tar_reset();
  tar_add("content.md", "ciphertext");
  tar_add("schema.yaml", "sections:\n  - id: terms\n    required: true\n");
  tar_add("encryption/policy.yaml", "encrypted_files:\n  - content.md\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  log.n = 0;
  CHECK_ST(mcdf_micro_validate_core(r, log_issue, &log, NULL), MCDF_MICRO_OK);
  CHECK(log.n == 1);
  CHECK(log.codes[0] == MCDF_MICRO_E_UNIMPLEMENTED);

  /* The counter is usable on its own, for a caller with nowhere to put a
   * callback. */
  {
    size_t issues = 99;
    CHECK_ST(mcdf_micro_validate_core(r, NULL, NULL, &issues), MCDF_MICRO_OK);
    CHECK(issues == 1);
  }
}

/* ---------------------------------------------------- the Integrity gate */

static void test_manifest_membership(void) {
  section("manifest membership (normative, conformance/errors.md)");

  CHECK(mcdf_micro_manifest_excluded("manifest.json"));
  CHECK(mcdf_micro_manifest_excluded("audit.log"));
  CHECK(mcdf_micro_manifest_excluded("audit.checkpoint"));
  CHECK(mcdf_micro_manifest_excluded("signatures/author.sig"));
  CHECK(mcdf_micro_manifest_excluded("signatures/"));

  CHECK(!mcdf_micro_manifest_excluded("content.md"));
  CHECK(!mcdf_micro_manifest_excluded("metadata.yaml"));
  CHECK(!mcdf_micro_manifest_excluded("signatures"));
  CHECK(!mcdf_micro_manifest_excluded("assets/signatures/x.png"));
  CHECK(!mcdf_micro_manifest_excluded("audit.log.bak"));
  CHECK(!mcdf_micro_manifest_excluded(""));
}

static void test_integrity(void) {
  struct fixture f;
  struct issue_log log;
  char hex[MCDF_MICRO_SHA256_HEX_SIZE];
  mcdf_micro_sha256 sha;
  unsigned char digest[MCDF_MICRO_SHA256_SIZE];
  size_t i;

  section("integrity: SHA-256 and manifest verification");

#if !defined(MCDF_MICRO_HAS_INTEGRITY)
  /* The gate is off. Every entry point still exists - which is what lets one
   * piece of caller code compile against every configuration - and every one
   * of them refuses rather than pretending. */
  CHECK_ST(mcdf_micro_sha256_init(&sha), MCDF_MICRO_E_DISABLED);
  CHECK_ST(mcdf_micro_sha256_update(&sha, "abc", 3), MCDF_MICRO_E_DISABLED);
  CHECK_ST(mcdf_micro_sha256_final(&sha, digest), MCDF_MICRO_E_DISABLED);
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_INTEGRITY) == 0);
  if (fixture_open(&f, VECTOR("valid/minimal"))) {
    CHECK_ST(mcdf_micro_hash_member(f.r, "content.md", hex, sizeof hex),
             MCDF_MICRO_E_DISABLED);
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_E_DISABLED);
    fixture_close(&f);
  }
  (void)i;
#else
  /* FIPS 180-4 known answers, then the same digest reached one byte at a time,
   * because the streaming path is the one a device actually uses. */
  CHECK_ST(mcdf_micro_sha256_init(&sha), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_sha256_update(&sha, "abc", 3), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_sha256_final(&sha, digest), MCDF_MICRO_OK);
  CHECK(digest[0] == 0xBA && digest[1] == 0x78 && digest[2] == 0x16 &&
        digest[31] == 0xAD);

  CHECK_ST(mcdf_micro_sha256_init(&sha), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_sha256_final(&sha, digest), MCDF_MICRO_OK);
  CHECK(digest[0] == 0xE3 && digest[1] == 0xB0 && digest[31] == 0x55);

  {
    /* 448 bits, the boundary where the length block needs a second pass. */
    static const char kLong[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK_ST(mcdf_micro_sha256_init(&sha), MCDF_MICRO_OK);
    for (i = 0; i < sizeof kLong - 1; ++i) {
      CHECK_ST(mcdf_micro_sha256_update(&sha, kLong + i, 1), MCDF_MICRO_OK);
    }
    CHECK_ST(mcdf_micro_sha256_final(&sha, digest), MCDF_MICRO_OK);
    CHECK(digest[0] == 0x24 && digest[1] == 0x8D && digest[31] == 0xC1);
  }

  /* A digest the reference runtime wrote, recomputed here. This is the check
   * that matters: two implementations agreeing on bytes neither wrote for the
   * other. */
  if (fixture_open(&f, VECTOR("canonical/manifest"))) {
    CHECK_ST(mcdf_micro_hash_member(f.r, "content.md", hex, sizeof hex),
             MCDF_MICRO_OK);
    CHECK(strcmp(hex,
                 "a35970c64c935487af43f4cfcbae9adb825b446065174cd60801bf22d98"
                 "bbf06") == 0);
    CHECK_ST(mcdf_micro_hash_member(f.r, "nope.md", hex, sizeof hex),
             MCDF_MICRO_E_NOT_FOUND);
    CHECK_ST(mcdf_micro_hash_member(f.r, "content.md", hex, 16),
             MCDF_MICRO_E_RANGE);
    fixture_close(&f);
  }

  if (fixture_open(&f, VECTOR("valid/minimal"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 0);
    fixture_close(&f);
  }

  if (fixture_open(&f, VECTOR("invalid/hash-mismatch"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 1);
    CHECK(logged(&log, MCDF_MICRO_E_MANIFEST_HASH_MISMATCH, "content.md"));
    fixture_close(&f);
  }

  if (fixture_open(&f, VECTOR("invalid/extra-file"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 1);
    CHECK(logged(&log, MCDF_MICRO_E_MANIFEST_EXTRA_FILE, "extra.txt"));
    fixture_close(&f);
  }

  /* An algorithm off the allow-list stops the walk: nothing below that line
   * could mean anything, and reporting every file as a mismatch would bury the
   * one fact that explains all of them. */
  if (fixture_open(&f, VECTOR("invalid/bad-algo"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 1);
    CHECK(log.codes[0] == MCDF_MICRO_E_ALGO_NOT_ALLOWED);
    fixture_close(&f);
  }

  /* Signatures are exempt from the manifest, so a signed container verifies at
   * Integrity without this reader knowing anything about signatures. */
  if (fixture_open(&f, VECTOR("valid/signed"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 0);
    fixture_close(&f);
  }

  /* So is an encrypted one: the manifest hashes the ciphertext, which is what
   * makes Integrity hold over a sealed document without a key. */
  if (fixture_open(&f, VECTOR("valid/encrypted"))) {
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(f.r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 0);
    fixture_close(&f);
  }

  {
    mcdf_micro_source src;
    struct mem_source mem;
    mcdf_micro_reader *r = NULL;

    tar_reset();
    tar_add("content.md", "x\n");
    tar_end();
    tar_source(&src, &mem);
    CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 1);
    CHECK(logged(&log, MCDF_MICRO_E_MISSING_MANIFEST, "manifest.json"));

    tar_reset();
    tar_add("content.md", "x\n");
    tar_add("manifest.json",
            "{\"files\":{\"content.md\":\"00\",\"gone.md\":\"11\"},"
            "\"hash_algorithm\":\"sha256\",\"mcdf_version\":\"1.0\"}");
    tar_end();
    tar_source(&src, &mem);
    CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
    log.n = 0;
    CHECK_ST(mcdf_micro_verify_manifest(r, log_issue, &log, NULL),
             MCDF_MICRO_OK);
    CHECK(log.n == 2);
    CHECK(logged(&log, MCDF_MICRO_E_MANIFEST_HASH_MISMATCH, "content.md"));
    CHECK(logged(&log, MCDF_MICRO_E_MANIFEST_MISSING_FILE, "gone.md"));
  }
#endif
}

/* ------------------------------------------------------- the event stream */

/* Flattens the stream into one line so a whole document's structure can be
 * asserted as a string. Structure is what this layer produces; checking it any
 * other way means writing a renderer inside the test. */
static char   g_events[4096];
static size_t g_events_len;
static int    g_abort_after; /* 0 = never abort */
static int    g_event_count;

static void ev_add(const char *s, size_t len) {
  size_t i;
  for (i = 0; i < len && g_events_len + 1 < sizeof g_events; ++i) {
    g_events[g_events_len++] = s[i];
  }
  g_events[g_events_len] = '\0';
}

static void ev_str(const char *s) { ev_add(s, strlen(s)); }

static int ev_stop(void) {
  ++g_event_count;
  return (g_abort_after != 0 && g_event_count >= g_abort_after) ? 1 : 0;
}

static int t_enter_block(void *ctx, mcdf_micro_block type, const void *d) {
  static const char *const names[] = {"doc", "quote", "ul", "ol", "li",
                                      "hr",  "h",     "code", "p"};
  (void)ctx;
  ev_str("[+");
  ev_str(names[type]);
  if (type == MCDF_MICRO_BLOCK_H && d != NULL) {
    char buf[8];
    sprintf(buf, ":%d", ((const mcdf_micro_heading_detail *)d)->level);
    ev_str(buf);
  }
  ev_str("]");
  return ev_stop();
}

static int t_leave_block(void *ctx, mcdf_micro_block type, const void *d) {
  static const char *const names[] = {"doc", "quote", "ul", "ol", "li",
                                      "hr",  "h",     "code", "p"};
  (void)ctx;
  ev_str("[-");
  ev_str(names[type]);
  if (type == MCDF_MICRO_BLOCK_H && d != NULL) {
    const mcdf_micro_heading_detail *h = (const mcdf_micro_heading_detail *)d;
    if (h->id.len > 0) {
      ev_str("#");
      ev_add(h->id.text, h->id.len);
    }
  }
  ev_str("]");
  return ev_stop();
}

static int t_enter_span(void *ctx, mcdf_micro_span type, const void *d) {
  static const char *const names[] = {"em", "strong", "link", "image", "code"};
  (void)ctx;
  ev_str("[+");
  ev_str(names[type]);
  if ((type == MCDF_MICRO_SPAN_LINK || type == MCDF_MICRO_SPAN_IMAGE) &&
      d != NULL) {
    const mcdf_micro_link_detail *l = (const mcdf_micro_link_detail *)d;
    ev_str(l->is_member ? " member=" : " extern=");
    ev_add(l->href.text, l->href.len);
  }
  ev_str("]");
  return ev_stop();
}

static int t_leave_span(void *ctx, mcdf_micro_span type, const void *d) {
  static const char *const names[] = {"em", "strong", "link", "image", "code"};
  (void)ctx; (void)d;
  ev_str("[-");
  ev_str(names[type]);
  ev_str("]");
  return ev_stop();
}

static int t_text(void *ctx, mcdf_micro_text_type type, const char *text,
                  size_t len) {
  (void)ctx;
  if (type == MCDF_MICRO_TEXT_SOFTBR) {
    ev_str("~");
  } else if (type == MCDF_MICRO_TEXT_BR) {
    ev_str("\\\\");
  } else {
    ev_add(text, len);
  }
  return ev_stop();
}

/* Renders the container's content.md and leaves the flattened stream in
 * g_events. */
static mcdf_micro_status ev_render(mcdf_micro_reader *r, void *buf,
                                   size_t buf_len) {
  mcdf_micro_render_callbacks cb;
  cb.enter_block = t_enter_block;
  cb.leave_block = t_leave_block;
  cb.enter_span = t_enter_span;
  cb.leave_span = t_leave_span;
  cb.text = t_text;
  g_events_len = 0;
  g_events[0] = '\0';
  g_event_count = 0;
  return mcdf_micro_render(r, buf, buf_len, &cb, NULL);
}

static void test_render(void) {
  mcdf_micro_source src;
  struct mem_source mem;
  mcdf_micro_reader *r = NULL;
  static unsigned char doc[8192];
  size_t need = 0;

  section("render: the block/span event stream");

  tar_reset();
  tar_add("content.md", "# Title {#top}\n\nHello.\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);

#if !defined(MCDF_MICRO_HAS_RENDER)
  /* The gate is off. Both entry points still exist - which is what lets one
   * piece of caller code compile against every configuration - and both
   * refuse rather than pretending. md4c is not in this build at all. */
  CHECK_ST(mcdf_micro_render_size(r, &need), MCDF_MICRO_E_DISABLED);
  CHECK(need == 0);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_E_DISABLED);
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_RENDER) == 0);
  {
    mcdf_micro_render_iter it;
    int done = 0;
    mcdf_micro_render_callbacks cb;
    cb.enter_block = t_enter_block;
    cb.leave_block = t_leave_block;
    cb.enter_span = t_enter_span;
    cb.leave_span = t_leave_span;
    cb.text = t_text;
    CHECK_ST(mcdf_micro_render_begin(r, doc, sizeof doc, &it),
             MCDF_MICRO_E_DISABLED);
    CHECK_ST(mcdf_micro_render_next(&it, &cb, NULL, &done),
             MCDF_MICRO_E_DISABLED);
    CHECK(done);
  }
  (void)ev_render;
#else
  CHECK((mcdf_micro_features() & MCDF_MICRO_FEATURE_RENDER) != 0);

  /* The buffer is the caller's, and its size is exactly content.md's. */
  CHECK_ST(mcdf_micro_render_size(r, &need), MCDF_MICRO_OK);
  CHECK(need == strlen("# Title {#top}\n\nHello.\n"));

  /* A buffer that cannot hold the document is refused, never truncated. */
  CHECK_ST(ev_render(r, doc, need - 1), MCDF_MICRO_E_RANGE);

  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
  /* The anchor is stripped from the heading's text and reported as an id -
   * and only on leave, because it sits at the end of the heading and cannot
   * be known before the text has been read (spec 10.4). */
  CHECK(strcmp(g_events,
               "[+doc][+h:1]Title[-h#top][+p]Hello.[-p][-doc]") == 0);

  /* Inline structure, and a container asset told apart from an external URL:
   * a layout engine can read the first with mcdf_micro_read_at and must not
   * fetch the second (spec 4.1). */
  tar_reset();
  tar_add("content.md",
          "Text with *em* and `code`.\n"
          "\n"
          "![Diagram](assets/d.png)\n"
          "\n"
          "[Away](https://example.org/x) and [Escape](../outside.png)\n");
  tar_add("assets/d.png", "\x89PNG-not-really");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
  CHECK(strcmp(g_events,
               "[+doc]"
               "[+p]Text with [+em]em[-em] and [+code]code[-code].[-p]"
               "[+p][+image member=assets/d.png]Diagram[-image][-p]"
               "[+p][+link extern=https://example.org/x]Away[-link]"
               " and [+link extern=../outside.png]Escape[-link][-p]"
               "[-doc]") == 0);

  /* Raw HTML is text, not markup (spec 10.4) - the same reading the reference
   * renderer takes, because two implementations have to agree on what the
   * document *is* before they can agree on anything else. */
  tar_reset();
  tar_add("content.md", "<div>not a tag</div>\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
  CHECK(strcmp(g_events, "[+doc][+p]<div>not a tag</div>[-p][-doc]") == 0);

  /* Every heading is reported with its own anchor, nested ones included. That
   * is the render rule, and it is deliberately not the binding rule: only a
   * top-level heading binds a schema section (spec 4.2). Two questions, two
   * answers, from the same document. */
  tar_reset();
  tar_add("content.md", "# Top {#top}\n\n> ## Quoted {#quoted}\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
  CHECK(strcmp(g_events,
               "[+doc][+h:1]Top[-h#top][+quote][+h:2]Quoted[-h#quoted]"
               "[-quote][-doc]") == 0);
  CHECK_ST(mcdf_micro_has_anchor(r, "top"), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_has_anchor(r, "quoted"), MCDF_MICRO_E_NOT_FOUND);

  /* The anchor is split off the *last* text chunk, and one chunk of lookahead
   * is all it takes: an anchor is plain text, so md4c never splits one across
   * chunks however much markup precedes it. These are the shapes that would
   * break if that were wrong. */
  tar_reset();
  tar_add("content.md",
          "## *Em* {#a}\n"
          "\n"
          "### A *b* c {#b}\n"
          "\n"
          "#### C# {#c}\n"
          "\n"
          "##### Trailing hash is not an anchor #\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
  CHECK(strcmp(g_events,
               "[+doc]"
               "[+h:2][+em]Em[-em][-h#a]"
               "[+h:3]A [+em]b[-em] c[-h#b]"
               "[+h:4]C#[-h#c]"
               "[+h:5]Trailing hash is not an anchor[-h]"
               "[-doc]") == 0);

  /* A heading whose anchor is its whole text leaves no text behind. */
  tar_reset();
  tar_add("content.md", "## {#only}\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
  CHECK(strcmp(g_events, "[+doc][+h:2][-h#only][-doc]") == 0);

  /* A callback that stops is not a broken document: md_parse reports an abort
   * and an allocation failure the same way, and telling them apart is the
   * difference between "you asked me to stop" and "your file is bad". */
  tar_reset();
  tar_add("content.md", "# One {#a}\n\nTwo\n\nThree\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  g_abort_after = 3;
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_E_ABORTED);
  g_abort_after = 0;

  /* Ciphertext fed to a CommonMark parser yields confident nonsense rather
   * than an error, so a sealed content.md is refused before the parse
   * (spec 6). */
  tar_reset();
  tar_add("content.md", "not really ciphertext, but the policy says it is");
  tar_add("encryption/policy.yaml", "encrypted_files:\n  - content.md\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_render_size(r, &need), MCDF_MICRO_E_CONTENT_SEALED);
  CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_E_CONTENT_SEALED);

  /* No content.md is not a render failure to blame on the renderer. */
  tar_reset();
  tar_add("metadata.yaml", "title: x\n");
  tar_end();
  tar_source(&src, &mem);
  CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
  CHECK_ST(mcdf_micro_render_size(r, &need), MCDF_MICRO_E_NOT_FOUND);

  /* ---- a window at a time -------------------------------------------- */

  /* The claim the windowed form has to earn: for a document a device cannot
   * hold, walking it in windows produces *the same events in the same order*
   * as parsing it whole. Anything less and a reader shows something different
   * depending on how much RAM it happened to have. */
  {
    static char whole[sizeof g_events];
    static unsigned char win[256];
    mcdf_micro_render_iter it;
    mcdf_micro_render_callbacks cb;
    size_t guard;
    int done = 0;

    /* Deliberately awkward: a list and a block quote that must not be split,
     * a fenced block containing what looks like a boundary, and a link whose
     * reference definition sits at the very end of the document. */
    tar_reset();
    tar_add("content.md",
            "# One {#one}\n"
            "\n"
            "First paragraph, long enough to matter for a small window.\n"
            "\n"
            "- alpha\n"
            "- beta\n"
            "\n"
            "- gamma\n"
            "\n"
            "> quoted line one\n"
            ">\n"
            "> quoted line two\n"
            "\n"
            "```\n"
            "not a boundary\n"
            "\n"
            "still inside the fence\n"
            "```\n"
            "\n"
            "## Two {#two}\n"
            "\n"
            "A paragraph using [the link][ref] defined at the end.\n"
            "\n"
            "Another paragraph so the tail is not the interesting case.\n"
            "\n"
            "[ref]: https://example.org/target\n");
    tar_end();
    tar_source(&src, &mem);
    CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);

    CHECK_ST(ev_render(r, doc, sizeof doc), MCDF_MICRO_OK);
    memcpy(whole, g_events, g_events_len + 1);
    /* The reference definition resolved, and emitted nothing of its own. */
    CHECK(strstr(whole, "[+link extern=https://example.org/target]") != NULL);

    cb.enter_block = t_enter_block;
    cb.leave_block = t_leave_block;
    cb.enter_span = t_enter_span;
    cb.leave_span = t_leave_span;
    cb.text = t_text;

    g_events_len = 0;
    g_events[0] = '\0';
    g_event_count = 0;
    CHECK_ST(mcdf_micro_render_begin(r, win, sizeof win, &it), MCDF_MICRO_OK);
    for (guard = 0; guard < 64 && !done; ++guard) {
      CHECK_ST(mcdf_micro_render_next(&it, &cb, NULL, &done), MCDF_MICRO_OK);
    }
    CHECK(done);
    /* More than one window, or the test proves nothing. */
    CHECK(guard > 1);
    if (strcmp(g_events, whole) != 0) {
      size_t d = 0, k, from;
      while (whole[d] != '\0' && whole[d] == g_events[d]) ++d;
      from = d > 40 ? d - 40 : 0;
      printf("  diverge at %lu\n", (unsigned long)d);
      for (k = 0; k < 2; ++k) {
        const char *s = k == 0 ? whole : g_events;
        size_t i;
        printf("    %-9s", k == 0 ? "whole:" : "windowed:");
        for (i = from; i < from + 120 && s[i] != '\0'; ++i) {
          if (s[i] == '\n') fputs("\\n", stdout);
          else fputc(s[i], stdout);
        }
        printf("\n");
      }
    }
    CHECK(strcmp(g_events, whole) == 0);

    /* The buffer's real requirement is the largest single top-level block, not
     * the document - so one paragraph longer than the buffer is the case that
     * cannot be served, and it is told so rather than handed a parse of
     * something the document does not contain. */
    {
      static unsigned char tiny[128];
      static char para[400];
      mcdf_micro_render_iter small;
      int small_done = 0;
      mcdf_micro_status st2;

      memset(para, 'x', sizeof para - 2);
      para[sizeof para - 2] = '\n';
      para[sizeof para - 1] = '\0';
      tar_reset();
      tar_add("content.md", para);
      tar_end();
      tar_source(&src, &mem);
      CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r),
               MCDF_MICRO_OK);

      st2 = mcdf_micro_render_begin(r, tiny, sizeof tiny, &small);
      CHECK_ST(st2, MCDF_MICRO_OK);
      CHECK_ST(mcdf_micro_render_next(&small, &cb, NULL, &small_done),
               MCDF_MICRO_E_RANGE);

      /* ...and the same document walks fine once the buffer can hold that
       * block - which is the whole point: the bound is the block, not the
       * document, and it is the caller's to satisfy. */
      CHECK_ST(mcdf_micro_render_begin(r, doc, sizeof doc, &small),
               MCDF_MICRO_OK);
      g_events_len = 0;
      g_events[0] = '\0';
      small_done = 0;
      CHECK_ST(mcdf_micro_render_next(&small, &cb, NULL, &small_done),
               MCDF_MICRO_OK);
      CHECK(small_done);
      CHECK(strncmp(g_events, "[+doc][+p]xxx", 13) == 0);
    }

    /* A document with no reference definitions carries no prefix at all. */
    tar_reset();
    tar_add("content.md", "# Plain {#p}\n\nJust prose.\n");
    tar_end();
    tar_source(&src, &mem);
    CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
    CHECK_ST(mcdf_micro_render_begin(r, win, sizeof win, &it), MCDF_MICRO_OK);
    CHECK(it.prefix_len == 0);
    g_events_len = 0;
    g_events[0] = '\0';
    done = 0;
    CHECK_ST(mcdf_micro_render_next(&it, &cb, NULL, &done), MCDF_MICRO_OK);
    CHECK(done);
    CHECK(strcmp(g_events, "[+doc][+h:1]Plain[-h#p][+p]Just prose.[-p][-doc]") ==
          0);

    /* Sealed content is refused here too - the refusal belongs to the document,
     * not to which entry point asked. */
    tar_reset();
    tar_add("content.md", "ciphertext");
    tar_add("encryption/policy.yaml", "encrypted_files:\n  - content.md\n");
    tar_end();
    tar_source(&src, &mem);
    CHECK_ST(mcdf_micro_open(&src, g_arena, sizeof g_arena, &r), MCDF_MICRO_OK);
    CHECK_ST(mcdf_micro_render_begin(r, win, sizeof win, &it),
             MCDF_MICRO_E_CONTENT_SEALED);
  }

  /* A real archive the reference runtime produced, end to end. */
  {
    struct fixture f;
    if (fixture_open(&f, VECTOR("canonical/render"))) {
      CHECK_ST(mcdf_micro_render_size(f.r, &need), MCDF_MICRO_OK);
      CHECK(need > 0 && need <= sizeof doc);
      CHECK_ST(ev_render(f.r, doc, sizeof doc), MCDF_MICRO_OK);
      /* Structure the reference's own canonical HTML also shows. */
      CHECK(strstr(g_events, "[-h#top]") != NULL);
      CHECK(strstr(g_events, "[-h#lists]") != NULL);
      CHECK(strstr(g_events, "[+image member=assets/diagram.png]") != NULL);
      CHECK(strstr(g_events, "<div>not a tag</div>") != NULL);
      fixture_close(&f);
    }
  }
#endif
}

static void test_reporting(void) {
  const uint32_t bits = mcdf_micro_features();

  section("what this build says it is");

  /* A build must never claim a profile whose code is not compiled in, and the
   * answer comes from the gates rather than from this file asserting it. */
  CHECK((bits & MCDF_MICRO_FEATURE_CORE) != 0);
#if defined(MCDF_MICRO_HAS_INTEGRITY)
  CHECK((bits & MCDF_MICRO_FEATURE_INTEGRITY) != 0);
#else
  CHECK((bits & MCDF_MICRO_FEATURE_INTEGRITY) == 0);
#endif
  CHECK((bits & MCDF_MICRO_FEATURE_SIGNED) == 0);
#if defined(MCDF_MICRO_HAS_RENDER)
  CHECK((bits & MCDF_MICRO_FEATURE_RENDER) != 0);
#else
  CHECK((bits & MCDF_MICRO_FEATURE_RENDER) == 0);
#endif

  /* Kit codes keep the kit's spelling, so a harness can print them straight. */
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_OK), "OK") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_MISSING_CONTENT),
               "E_MISSING_CONTENT") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_SCHEMA_UNBOUND),
               "E_SCHEMA_UNBOUND") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_REQUIRED_SECTION_MISSING),
               "E_REQUIRED_SECTION_MISSING") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_ASSET_PATH_ESCAPE),
               "E_ASSET_PATH_ESCAPE") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_MISSING_MANIFEST),
               "E_MISSING_MANIFEST") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_MANIFEST_HASH_MISMATCH),
               "E_MANIFEST_HASH_MISMATCH") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_MANIFEST_MISSING_FILE),
               "E_MANIFEST_MISSING_FILE") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_MANIFEST_EXTRA_FILE),
               "E_MANIFEST_EXTRA_FILE") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_ALGO_NOT_ALLOWED),
               "E_ALGO_NOT_ALLOWED") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_CONTENT_SEALED),
               "E_CONTENT_SEALED") == 0);
  CHECK(strcmp(mcdf_micro_status_str(MCDF_MICRO_E_ABORTED), "E_ABORTED") == 0);
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
  test_schema();
  test_anchors();
  test_sealed();
  test_validate_core();
  test_manifest_membership();
  test_integrity();
  test_render();
  test_path_safety();
  test_reporting();

  printf("\n-------------------------------------\n");
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}
