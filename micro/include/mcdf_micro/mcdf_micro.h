/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */
#ifndef MCDF_MICRO_H
#define MCDF_MICRO_H

/*
 * mcdf_micro - a portable C99 reader for MCDF containers.
 *
 * Reads the USTAR interchange form of an .mcdf container (spec section 3) on
 * parts far too small for the desktop runtime: the whole container is never
 * held in RAM, nothing is allocated, and no vendor header appears anywhere in
 * the core. Three properties make that true:
 *
 *   1. Bytes arrive through a caller-supplied read callback, so an SD card, a
 *      memory-mapped partition and a host FILE* are all the same source.
 *   2. All state lives in a caller-supplied arena. The core never calls
 *      malloc, on any target.
 *   3. Every entry point returns a status code. Nothing throws, longjmps or
 *      writes errno, and no callback may throw across the boundary.
 *
 * Read-only, forever: there is no authoring, packing, signing or encryption
 * here, and there never will be.
 *
 * The header compiles as C99 and as C++23. C++ callers get an opaque handle
 * and an explicit destroy, which wraps into a unique_ptr in three lines.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ version */

#define MCDF_MICRO_VERSION_MAJOR 0
#define MCDF_MICRO_VERSION_MINOR 1
#define MCDF_MICRO_VERSION_PATCH 0

/* The MCDF format version this reader targets. */
#define MCDF_MICRO_MCDF_VERSION "1.0"

/* ----------------------------------------------------------------- features */

/* Feature bits, named after the spec's profile ladder so a build's
 * configuration and the conformance vocabulary are the same vocabulary.
 * mcdf_micro_features() reports what this build actually does - a harness that
 * needs to know which profiles to claim asks the library, never the docs. */
#define MCDF_MICRO_FEATURE_CORE      0x1u
#define MCDF_MICRO_FEATURE_INTEGRITY 0x2u
#define MCDF_MICRO_FEATURE_SIGNED    0x4u
#define MCDF_MICRO_FEATURE_RENDER    0x8u

/* Compile-time counterparts, for callers that branch rather than query. A
 * macro appears here only when the code behind it is in the build, so nothing
 * here can promise a capability that was not compiled. */
#define MCDF_MICRO_HAS_CORE 1

uint32_t mcdf_micro_features(void);

/* ------------------------------------------------------------------- status */

/* Status codes map onto conformance/errors.md wherever the kit defines one, so
 * a scoring harness reports what the kit expects rather than a parallel
 * vocabulary. mcdf_micro_status_str() returns the kit spelling for those, and
 * a library-specific name for conditions the kit has no code for (a malformed
 * archive is not a document defect the kit names). */
typedef enum mcdf_micro_status {
  MCDF_MICRO_OK = 0,

  /* Library conditions. */
  MCDF_MICRO_E_INVAL,      /* a NULL or nonsensical argument */
  MCDF_MICRO_E_IO,         /* the read callback reported failure */
  MCDF_MICRO_E_ARENA,      /* the arena cannot hold this container's index */
  MCDF_MICRO_E_PARSE,      /* the container is not a well-formed USTAR archive */
  MCDF_MICRO_E_NOT_FOUND,  /* no such member, key or index */
  MCDF_MICRO_E_RANGE,      /* read past the end of a member, or value too long
                              for the destination (see *out_len) */

  /* Kit codes (conformance/errors.md). Reserved here so the mapping exists
     from the start; the validation that raises them lands with the CLI. */
  MCDF_MICRO_E_MISSING_CONTENT,
  MCDF_MICRO_E_ASSET_PATH_ESCAPE,

  /* Honest-gap signals. E_DISABLED means a feature gate removed this code
     from the build; E_UNIMPLEMENTED means this build did not evaluate a
     profile. Neither ever means "the document is wrong" - see the partial
     implementations table in conformance/errors.md. */
  MCDF_MICRO_E_DISABLED,
  MCDF_MICRO_E_UNIMPLEMENTED
} mcdf_micro_status;

/* Never NULL, never allocates; safe to print directly. */
const char *mcdf_micro_status_str(mcdf_micro_status status);

/* ------------------------------------------------------------------- limits */

/* A metadata.yaml line, or a manifest.json key. 255 is the longest path USTAR
 * can express, so this cannot truncate a member path. */
#define MCDF_MICRO_LINE_MAX 256

/* The longest manifest.json scalar value this reader will hand back. A sha256
 * hex digest is 64 bytes; the version and algorithm fields are shorter. A
 * longer value is reported E_RANGE rather than silently truncated. */
#define MCDF_MICRO_VALUE_MAX 128

/* ------------------------------------------------------------------- source */

/* Reads `len` bytes at absolute container offset `off` into `dst`.
 * Returns 0 on success, non-zero on I/O failure. Must not throw or longjmp.
 * A short read is a failure: the reader never asks for more than it verified
 * is inside the container. */
typedef int (*mcdf_micro_read_fn)(void *ctx, uint64_t off, void *dst, size_t len);

typedef struct mcdf_micro_source {
  void               *ctx;   /* SD handle, FILE*, base pointer, ... */
  mcdf_micro_read_fn  read;
  uint64_t            size;  /* total container bytes */
} mcdf_micro_source;

/* ------------------------------------------------------------------- reader */

typedef struct mcdf_micro_reader mcdf_micro_reader;

typedef struct mcdf_micro_member_info {
  const char *path;    /* NUL-terminated; owned by the reader's arena */
  uint64_t    offset;  /* absolute offset of the member's data */
  uint64_t    size;    /* member length in bytes */
} mcdf_micro_member_info;

/* Arena sizing. The contract is a fixed caller arena - there is no allocator
 * hook and no runtime query - so firmware sizes it from what it knows about
 * the documents it will carry:
 *
 *   static uint8_t arena[MCDF_MICRO_ARENA_SIZE(64, 2048)];   // 64 members,
 *                                                            // 2 KB of paths
 *
 * `path_bytes` counts every member path plus its NUL. Both constants are
 * documented upper bounds, not exact sizes; a build asserts the real structs
 * fit under them. Over-estimating wastes bytes, under-estimating returns
 * MCDF_MICRO_E_ARENA - never corruption. */
#define MCDF_MICRO_ARENA_OVERHEAD 128
#define MCDF_MICRO_ENTRY_SIZE      24
#define MCDF_MICRO_ARENA_SIZE(members, path_bytes) \
  ((size_t)MCDF_MICRO_ARENA_OVERHEAD +             \
   (size_t)(members) * (size_t)MCDF_MICRO_ENTRY_SIZE + (size_t)(path_bytes))

/* Walks the archive once, building an index of (path, offset, size) in the
 * arena. No member data is read or held. The arena and the source must both
 * outlive the reader; the reader keeps pointers into the arena, not copies.
 *
 * Returns E_PARSE on a malformed archive, E_ARENA if the index does not fit,
 * E_IO if the callback fails. `*out` is only written on success. */
mcdf_micro_status mcdf_micro_open(const mcdf_micro_source *src,
                                  void *arena, size_t arena_len,
                                  mcdf_micro_reader **out);

/* Invalidates the handle. The arena is the caller's and is not touched beyond
 * being poisoned; passing NULL is a no-op. */
void mcdf_micro_close(mcdf_micro_reader *reader);

/* Number of indexed members. Directory entries and other non-regular archive
 * entries are skipped, matching the reference runtime's tar reader. */
size_t mcdf_micro_count(mcdf_micro_reader *reader);

/* Index-order access; order is the archive's own (the reference packer writes
 * members sorted by path). E_NOT_FOUND when `index` is past the end. */
mcdf_micro_status mcdf_micro_at(mcdf_micro_reader *reader, size_t index,
                                mcdf_micro_member_info *out);

/* Exact-match lookup by container-relative path, e.g. "content.md" or
 * "assets/logo.png". E_NOT_FOUND if absent. */
mcdf_micro_status mcdf_micro_member(mcdf_micro_reader *reader, const char *path,
                                    mcdf_micro_member_info *out);

/* Reads `len` bytes from `off` within the member. E_RANGE if the window is not
 * wholly inside the member - a partial read is never reported as success. */
mcdf_micro_status mcdf_micro_read_at(mcdf_micro_reader *reader, const char *path,
                                     uint64_t off, void *dst, size_t len);

/* Returns non-zero if `path` is safe to use as a relative filesystem path:
 * not absolute, no ".." component, no drive letter, no backslash separator.
 * This reader never writes files, so it does not enforce the rule at open()
 * time - the reference runtime enforces it when extracting, and so should any
 * caller that extracts. Failing this is the kit's E_ASSET_PATH_ESCAPE. */
int mcdf_micro_path_is_safe(const char *path);

/* --------------------------------------------------------------- metadata */

/* metadata.yaml, read as the spec's small fixed shape rather than as YAML: a
 * top-level `key: value` scalar, and an `authors:` block sequence whose items
 * carry a `name:`. That is what a library screen needs; anything richer
 * belongs on a machine with a real parser.
 *
 * Both calls stream the member through a small window - nothing is cached and
 * the arena is untouched, so repeated calls cost repeated reads.
 *
 * `*out_len` receives the value's length in bytes, excluding the NUL, and is
 * set even when the value does not fit. On success `dst` is NUL-terminated.
 * E_RANGE means the value did not fit in `dst_len` or exceeded
 * MCDF_MICRO_LINE_MAX; E_NOT_FOUND means the key or author is absent. */
mcdf_micro_status mcdf_micro_meta(mcdf_micro_reader *reader, const char *key,
                                  char *dst, size_t dst_len, size_t *out_len);

mcdf_micro_status mcdf_micro_meta_author(mcdf_micro_reader *reader, size_t index,
                                         char *dst, size_t dst_len,
                                         size_t *out_len);

/* --------------------------------------------------------------- manifest */

/* manifest.json, read the same streaming way. This is the read side only:
 * hashing the members and comparing is the Integrity profile and lands behind
 * MCDF_MICRO_ENABLE_INTEGRITY in mcdf_micro_verify.h. */

/* A top-level scalar, e.g. "hash_algorithm" or "mcdf_version". */
mcdf_micro_status mcdf_micro_manifest_field(mcdf_micro_reader *reader,
                                            const char *key,
                                            char *dst, size_t dst_len,
                                            size_t *out_len);

/* The recorded hash for one member path, from the "files" object. */
mcdf_micro_status mcdf_micro_manifest_hash(mcdf_micro_reader *reader,
                                           const char *path,
                                           char *dst, size_t dst_len,
                                           size_t *out_len);

/* Number of entries in "files". */
mcdf_micro_status mcdf_micro_manifest_count(mcdf_micro_reader *reader,
                                            size_t *out);

/* The n-th "files" entry, in the order the manifest stores them (canonical
 * manifests are key-sorted). Either destination may be NULL to skip it; the
 * matching out_len is still filled in. */
mcdf_micro_status mcdf_micro_manifest_at(mcdf_micro_reader *reader, size_t index,
                                         char *path_dst, size_t path_dst_len,
                                         size_t *path_out_len,
                                         char *hash_dst, size_t hash_dst_len,
                                         size_t *hash_out_len);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MCDF_MICRO_H */
