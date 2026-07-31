/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The harness `conformance/run.sh` scores.
 *
 * It exposes the kit's verbs over `mcdf_micro` and nothing else - no authoring,
 * no conversion, no convenience. Its whole job is to let the conformance kit
 * ask this reader the same questions it asks the reference runtime and
 * `mcdf-ts`, and get answers in the same vocabulary.
 *
 * Two rules it exists to honour:
 *
 *   - **It never claims a profile it did not evaluate.** Signed, Encrypted and
 *     Render are reported E_UNIMPLEMENTED, which conformance/errors.md reserves
 *     for exactly this and which the runner scores as "not implemented" rather
 *     than as a pass or a failure. A silent pass would tell a user their
 *     document is trustworthy on the strength of a check that did not happen.
 *   - **It reports what the build actually is.** The feature gates decide which
 *     profiles it answers for, and it asks the library rather than assuming -
 *     an INTEGRITY=off build reports Integrity unimplemented too.
 *
 * TAR only, permanently and by design (spec 3 makes it the REQUIRED
 * serialization). A reader with no filesystem cannot walk a directory
 * container, which is the reason the kit publishes every vector packed. */

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "jcs.h"
#include "mcdf_micro/mcdf_micro.h"
#include "mcdf_micro/mcdf_micro_render.h"
#include "mcdf_micro/mcdf_micro_verify.h"

/* Sized for the conformance vectors and any document a host would hand this,
 * with room to spare: the arena is a published cost, and a CLI that guessed
 * small would be reporting an implementation limit as a document defect. */
static unsigned char g_arena[MCDF_MICRO_ARENA_SIZE(512, 32768)];

/* ------------------------------------------------------------------ source */

static int file_read(void *ctx, uint64_t off, void *dst, size_t len) {
  FILE *fp = (FILE *)ctx;
  if (fseek(fp, (long)off, SEEK_SET) != 0) return -1;
  return fread(dst, 1, len, fp) == len ? 0 : -1;
}

static int open_container(const char *path, FILE **out_fp,
                          mcdf_micro_source *out_src) {
  FILE *fp = fopen(path, "rb");
  long size;

  if (fp == NULL) {
    fprintf(stderr, "error: cannot open %s\n", path);
    return 0;
  }
  if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0) {
    /* A directory opens on some platforms and then refuses to seek. Naming the
     * reason beats "cannot open": a directory container is not a defect in the
     * document, it is a form this reader will never support. */
    fprintf(stderr,
            "error: %s is not a readable .mcdf archive; this reader supports "
            "only the TAR serialization (spec 3)\n",
            path);
    fclose(fp);
    return 0;
  }
  rewind(fp);

  out_src->ctx = fp;
  out_src->read = file_read;
  out_src->size = (uint64_t)size;
  *out_fp = fp;
  return 1;
}

/* ---------------------------------------------------------------- reporting */

/* Issues are collected before anything is printed, because the verdict line
 * carries the count and has to come first - the shape the reference CLI uses
 * and the kit's output is read in. */
#define ISSUE_LINES 64u
#define ISSUE_WIDTH 320u

static char   g_issues[ISSUE_LINES][ISSUE_WIDTH];
static size_t g_issue_count;

static void collect_issue(void *ctx, mcdf_micro_status code,
                          const char *detail) {
  (void)ctx;
  if (g_issue_count < ISSUE_LINES) {
    /* run.sh greps the combined output for the expected E_* code, so the kit
     * spelling has to appear here verbatim. */
    snprintf(g_issues[g_issue_count], ISSUE_WIDTH, "  %s: %s",
             mcdf_micro_status_str(code), detail);
  }
  ++g_issue_count;
}

static void print_issues(void) {
  size_t i;
  const size_t shown = g_issue_count < ISSUE_LINES ? g_issue_count : ISSUE_LINES;
  for (i = 0; i < shown; ++i) printf("%s\n", g_issues[i]);
  if (g_issue_count > shown) {
    printf("  (%lu more)\n", (unsigned long)(g_issue_count - shown));
  }
}

static int unimplemented(const char *profile) {
  printf("validate [%s]: E_UNIMPLEMENTED - this build evaluates %s\n", profile,
         (mcdf_micro_features() & MCDF_MICRO_FEATURE_INTEGRITY)
             ? "the core and integrity profiles only"
             : "the core profile only");
  return 1;
}

/* ------------------------------------------------------------------- verbs */

static int cmd_validate(const char *path, const char *profile) {
  FILE *fp = NULL;
  mcdf_micro_source src;
  mcdf_micro_reader *reader = NULL;
  mcdf_micro_status st;
  int wants_integrity;

  if (strcmp(profile, "core") == 0) {
    wants_integrity = 0;
  } else if (strcmp(profile, "integrity") == 0) {
    wants_integrity = 1;
  } else if (strcmp(profile, "signed") == 0 ||
             strcmp(profile, "encrypted") == 0 ||
             strcmp(profile, "render") == 0) {
    return unimplemented(profile);
  } else {
    fprintf(stderr, "error: unknown profile: %s\n", profile);
    return 2;
  }
  if (wants_integrity &&
      (mcdf_micro_features() & MCDF_MICRO_FEATURE_INTEGRITY) == 0) {
    return unimplemented(profile);
  }

  if (!open_container(path, &fp, &src)) return 1;
  st = mcdf_micro_open(&src, g_arena, sizeof g_arena, &reader);
  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s: %s\n", path, mcdf_micro_status_str(st));
    fclose(fp);
    return 1;
  }

  g_issue_count = 0;
  st = mcdf_micro_validate_core(reader, collect_issue, NULL, NULL);
  if (st == MCDF_MICRO_OK && wants_integrity) {
    st = mcdf_micro_verify_manifest(reader, collect_issue, NULL, NULL);
  }
  mcdf_micro_close(reader);
  fclose(fp);

  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s: %s\n", path, mcdf_micro_status_str(st));
    return 1;
  }
  if (g_issue_count == 0) {
    printf("validate [%s]: OK\n", profile);
    return 0;
  }
  printf("validate [%s]: %lu issue(s)\n", profile,
         (unsigned long)g_issue_count);
  print_issues();
  return 1;
}

static int cmd_manifest(const char *path) {
  FILE *fp = NULL;
  mcdf_micro_source src;
  mcdf_micro_reader *reader = NULL;
  mcdf_micro_status st;

  if ((mcdf_micro_features() & MCDF_MICRO_FEATURE_INTEGRITY) == 0) {
    fprintf(stderr,
            "error: E_UNIMPLEMENTED - building a manifest needs the integrity "
            "gate\n");
    return 1;
  }
  if (!open_container(path, &fp, &src)) return 1;
  st = mcdf_micro_open(&src, g_arena, sizeof g_arena, &reader);
  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s: %s\n", path, mcdf_micro_status_str(st));
    fclose(fp);
    return 1;
  }

  st = mcdf_micro_jcs_manifest(reader, stdout);
  mcdf_micro_close(reader);
  fclose(fp);
  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s\n", mcdf_micro_status_str(st));
    return 1;
  }
  /* The kit diffs this against expected/manifest.json, so stdout carries the
   * canonical bytes, one trailing newline, and nothing else. */
  fputc('\n', stdout);
  return 0;
}

/* ------------------------------------------------------------------ events */

/* Prints the block/span event stream. Not a renderer and not scored by the
 * kit - it is how a human (or a layout engine's author) sees what the stream
 * actually contains, and how the tests check it without a screen. */

static const char *kBlockName[] = {"doc",  "quote", "ul", "ol", "li",
                                   "hr",   "h",     "code", "p"};
static const char *kSpanName[] = {"em", "strong", "link", "image", "code"};
static const char *kTextName[] = {"normal", "nullchar", "br",
                                  "softbr", "entity",   "code"};

static int g_depth;

static void indent(void) {
  int i;
  for (i = 0; i < g_depth; ++i) fputs("  ", stdout);
}

/* Body text can contain anything; keep one event on one line. */
static void print_slice(const char *text, size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    const unsigned char c = (unsigned char)text[i];
    if (c == '\n')      fputs("\\n", stdout);
    else if (c == '\r') fputs("\\r", stdout);
    else if (c == '\t') fputs("\\t", stdout);
    else if (c < 0x20)  printf("\\x%02x", c);
    else                fputc((int)c, stdout);
  }
}

static int ev_enter_block(void *ctx, mcdf_micro_block type, const void *detail) {
  (void)ctx;
  indent();
  printf("+%s", kBlockName[type]);
  if (type == MCDF_MICRO_BLOCK_H && detail != NULL) {
    printf(" level=%d", ((const mcdf_micro_heading_detail *)detail)->level);
  } else if ((type == MCDF_MICRO_BLOCK_UL || type == MCDF_MICRO_BLOCK_OL) &&
             detail != NULL) {
    const mcdf_micro_list_detail *d = (const mcdf_micro_list_detail *)detail;
    printf(" tight=%d mark=%c", d->is_tight, d->mark ? d->mark : '?');
    if (type == MCDF_MICRO_BLOCK_OL) printf(" start=%u", d->start);
  } else if (type == MCDF_MICRO_BLOCK_CODE && detail != NULL) {
    const mcdf_micro_code_detail *d = (const mcdf_micro_code_detail *)detail;
    printf(" fence=%c info=\"", d->fence ? d->fence : '-');
    print_slice(d->info.text, d->info.len);
    fputc('"', stdout);
  }
  fputc('\n', stdout);
  ++g_depth;
  return 0;
}

static int ev_leave_block(void *ctx, mcdf_micro_block type, const void *detail) {
  (void)ctx;
  if (g_depth > 0) --g_depth;
  indent();
  printf("-%s", kBlockName[type]);
  /* The anchor is only known here: it sits at the end of the heading's text. */
  if (type == MCDF_MICRO_BLOCK_H && detail != NULL) {
    const mcdf_micro_heading_detail *d =
        (const mcdf_micro_heading_detail *)detail;
    if (d->id.len > 0) {
      fputs(" id=", stdout);
      print_slice(d->id.text, d->id.len);
    }
  }
  fputc('\n', stdout);
  return 0;
}

static void print_link(const void *detail) {
  const mcdf_micro_link_detail *d = (const mcdf_micro_link_detail *)detail;
  if (d == NULL) return;
  fputs(" href=\"", stdout);
  print_slice(d->href.text, d->href.len);
  printf("\" member=%d", d->is_member);
  if (d->title.len > 0) {
    fputs(" title=\"", stdout);
    print_slice(d->title.text, d->title.len);
    fputc('"', stdout);
  }
}

static int ev_enter_span(void *ctx, mcdf_micro_span type, const void *detail) {
  (void)ctx;
  indent();
  printf("+%s", kSpanName[type]);
  if (type == MCDF_MICRO_SPAN_LINK || type == MCDF_MICRO_SPAN_IMAGE) {
    print_link(detail);
  }
  fputc('\n', stdout);
  ++g_depth;
  return 0;
}

static int ev_leave_span(void *ctx, mcdf_micro_span type, const void *detail) {
  (void)ctx; (void)detail;
  if (g_depth > 0) --g_depth;
  indent();
  printf("-%s\n", kSpanName[type]);
  return 0;
}

static int ev_text(void *ctx, mcdf_micro_text_type type, const char *text,
                   size_t len) {
  (void)ctx;
  indent();
  printf("%s \"", kTextName[type]);
  print_slice(text, len);
  fputs("\"\n", stdout);
  return 0;
}

static int cmd_events(const char *path) {
  static unsigned char g_doc[1u << 20]; /* 1 MiB: a host, not a device */
  FILE *fp = NULL;
  mcdf_micro_source src;
  mcdf_micro_reader *reader = NULL;
  mcdf_micro_render_callbacks cb;
  mcdf_micro_status st;
  size_t need = 0;

  if ((mcdf_micro_features() & MCDF_MICRO_FEATURE_RENDER) == 0) {
    fprintf(stderr,
            "error: E_UNIMPLEMENTED - the event stream needs the render gate\n");
    return 1;
  }
  if (!open_container(path, &fp, &src)) return 1;
  st = mcdf_micro_open(&src, g_arena, sizeof g_arena, &reader);
  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s: %s\n", path, mcdf_micro_status_str(st));
    fclose(fp);
    return 1;
  }

  /* The document buffer is the caller's, always - the library will not
   * allocate it, and its size is the size of content.md. */
  st = mcdf_micro_render_size(reader, &need);
  if (st == MCDF_MICRO_OK && need > sizeof g_doc) st = MCDF_MICRO_E_RANGE;
  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s: %s\n", path, mcdf_micro_status_str(st));
    mcdf_micro_close(reader);
    fclose(fp);
    return 1;
  }

  cb.enter_block = ev_enter_block;
  cb.leave_block = ev_leave_block;
  cb.enter_span = ev_enter_span;
  cb.leave_span = ev_leave_span;
  cb.text = ev_text;

  g_depth = 0;
  st = mcdf_micro_render(reader, g_doc, sizeof g_doc, &cb, NULL);
  mcdf_micro_close(reader);
  fclose(fp);
  if (st != MCDF_MICRO_OK) {
    fprintf(stderr, "error: %s\n", mcdf_micro_status_str(st));
    return 1;
  }
  return 0;
}

static int cmd_features(void) {
  const uint32_t bits = mcdf_micro_features();
  printf("mcdf_micro %d.%d.%d (mcdf %s)\n", MCDF_MICRO_VERSION_MAJOR,
         MCDF_MICRO_VERSION_MINOR, MCDF_MICRO_VERSION_PATCH,
         MCDF_MICRO_MCDF_VERSION);
  printf("profiles:");
  if (bits & MCDF_MICRO_FEATURE_CORE) printf(" core");
  if (bits & MCDF_MICRO_FEATURE_INTEGRITY) printf(" integrity");
  if (bits & MCDF_MICRO_FEATURE_SIGNED) printf(" signed");
  if (bits & MCDF_MICRO_FEATURE_RENDER) printf(" render");
  printf("\n");
  return 0;
}

static int usage(FILE *to) {
  fputs(
      "mcdf_micro_cli - the conformance harness for the portable C reader\n"
      "\n"
      "Usage:\n"
      "  mcdf_micro_cli validate <container.mcdf> [--profile P]\n"
      "  mcdf_micro_cli manifest <container.mcdf>\n"
      "  mcdf_micro_cli render   <html|text> <container.mcdf>\n"
      "  mcdf_micro_cli events   <container.mcdf>\n"
      "  mcdf_micro_cli features\n"
      "\n"
      "  P is core|integrity|signed|encrypted|render (default: integrity).\n"
      "  Profiles and verbs this build does not implement - render always, and\n"
      "  integrity when its gate is off - report E_UNIMPLEMENTED, never a pass.\n"
      "  Containers are read in the TAR serialization only (spec 3).\n",
      to);
  return to == stderr ? 2 : 0;
}

int main(int argc, char **argv) {
  const char *profile = "integrity";
  const char *container = NULL;
  int i;

#if defined(_WIN32)
  /* `manifest` is a byte-exact format. Text mode would rewrite every LF as
   * CRLF and the kit would fail on Windows for a reason that has nothing to do
   * with the reader - the same defect the reference CLI once shipped. */
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  if (argc < 2) return usage(stderr);
  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    return usage(stdout);
  }
  if (strcmp(argv[1], "--version") == 0) return cmd_features();
  if (strcmp(argv[1], "features") == 0) return cmd_features();

  /* A verb the kit asks for and this build cannot answer is still a verb it
   * has to answer *about*. Falling through to "unknown command" would be
   * scored as a wrong render rather than as an absent one. */
  if (strcmp(argv[1], "render") == 0) {
    fprintf(stderr,
            "error: E_UNIMPLEMENTED - this build has no renderer; the Render "
            "profile arrives with the Markdown event stream\n");
    return 1;
  }

  for (i = 2; i < argc; ++i) {
    if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
      profile = argv[++i];
    } else if (strncmp(argv[i], "--profile=", 10) == 0) {
      profile = argv[i] + 10;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "error: unknown option: %s\n", argv[i]);
      return 2;
    } else if (container == NULL) {
      container = argv[i];
    } else {
      fprintf(stderr, "error: unexpected argument: %s\n", argv[i]);
      return 2;
    }
  }
  if (container == NULL) {
    fprintf(stderr, "error: a container is required\n");
    return 2;
  }

  if (strcmp(argv[1], "validate") == 0) return cmd_validate(container, profile);
  if (strcmp(argv[1], "manifest") == 0) return cmd_manifest(container);
  if (strcmp(argv[1], "events") == 0) return cmd_events(container);

  fprintf(stderr, "error: unknown command: %s\n", argv[1]);
  return usage(stderr);
}
