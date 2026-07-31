<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# `mcdf_micro` — a portable C99 reader for MCDF containers

A reader for the USTAR interchange form of an `.mcdf` container, small enough
for a microcontroller: no allocation, no filesystem, no dependencies, no vendor
headers. It is the third independent implementation of the format — after the
C++ engine and `mcdf-ts` — and it shares no code with either, which is the
whole point. An implementation that reused the engine's logic would prove
nothing about whether the spec is re-implementable.

**Read-only, forever.** No authoring, packing, signing or encryption lives here
and none ever will. Conversion happens upstream on a big CPU; the device only
ever sees `.mcdf`.

## The two contracts

**Bytes arrive through a callback.** Nothing here opens a file. An SD card, a
memory-mapped partition and a host `FILE*` are all the same three fields:

```c
static int sd_read(void *ctx, uint64_t off, void *dst, size_t len) {
  return f_lseek(ctx, off) || f_read(ctx, dst, len, &got) || got != len ? -1 : 0;
}

mcdf_micro_source src = {0};
src.ctx  = &file;
src.read = sd_read;
src.size = file_size;
```

**All state lives in an arena you provide.** The core never calls `malloc`, on
any target. Size the arena from what you know about the documents you carry —
member count and the total length of their paths:

```c
static uint8_t arena[MCDF_MICRO_ARENA_SIZE(64, 2048)];   /* 64 members, 2 KB of paths */

mcdf_micro_reader *r;
if (mcdf_micro_open(&src, arena, sizeof arena, &r) != MCDF_MICRO_OK) return;

mcdf_micro_member_info info;
if (mcdf_micro_member(r, "content.md", &info) == MCDF_MICRO_OK) {
  uint8_t page[512];
  mcdf_micro_read_at(r, "content.md", 0, page, sizeof page);
}

char title[64];
size_t len;
mcdf_micro_meta(r, "title", title, sizeof title, &len);
mcdf_micro_close(r);
```

There is no allocator hook and no runtime sizing query: a fixed arena is
stricter, and it forces an honest memory budget rather than deferring the
question to a part that cannot answer it. Under-estimating returns
`MCDF_MICRO_E_ARENA` — never a partial index, never corruption. The arena and
the source must both outlive the reader, which keeps pointers into the arena
rather than copies.

`mcdf_micro_open` walks the archive once, building an index of
`(path, offset, size)`. **No member data is read or held** — an illustrated
document indexes in a few hundred bytes regardless of how large its assets are.

## What it reads

| | |
|---|---|
| Container | USTAR walk with checksum verification, `prefix`/`name` reassembly, random access by path |
| `metadata.yaml` | top-level scalars and the `authors:` name list — enough for a library screen |
| `manifest.json` | recorded hashes by path, top-level fields, iteration over `files` |
| `schema.yaml` | the `sections:` list — each `id` and whether it is `required` |
| `content.md` | heading anchors: the `{#id}` that spec §4.2 binds a section to |
| `encryption/policy.yaml` | `encrypted_files` alone — which members are ciphertext and must not be parsed |

The YAML and JSON readers are deliberately not general parsers. They walk the
one shape the spec gives each file and refuse anything else rather than
guessing — a reader does not need a conforming YAML parser to show a title, and
putting one on a 2 MB part to get there would be the wrong trade. Both stream
through a 64-byte window, so nothing scales with member size.

The anchor scanner is a line scanner rather than a CommonMark parse, for the
same reason: an id is `[A-Za-z0-9._-]+`, so no markdown syntax can appear inside
one and there is nothing for an inline parser to do. It understands fenced and
indented code, ATX and setext headings, and closing sequences; it does not look
inside block quotes or list items, where no MCDF writer puts a bound section.
It will not be rerouted through md4c when the render gate lands — a build's
feature gates must never change which documents it considers valid.

Documented limits: a `metadata.yaml` or `schema.yaml` line may not exceed
`MCDF_MICRO_LINE_MAX` (256) bytes, a `manifest.json` scalar may not exceed
`MCDF_MICRO_VALUE_MAX` (128), and a section id may not exceed
`MCDF_MICRO_ID_MAX` (128); over that, the value is reported `E_RANGE` rather
than silently truncated. A value that simply does not fit the caller's buffer
is also `E_RANGE`, with `*out_len` set to the length it needed. A *heading* line
has no limit — the anchor is at the end of it, so the scanner keeps a rolling
tail rather than a prefix, and a verbose document is not a rejected one.

## What it checks

Two profiles, and it says so: `mcdf_micro_features()` reports what the build
actually compiled, never what the docs claim.

- **Core** — `mcdf_micro_validate_core()`: `content.md` is present, and every
  section `schema.yaml` declares binds to a heading anchor. No cryptography is
  involved, which is the profile ladder's central claim and the thing this
  library exists to demonstrate.
- **Integrity** — `mcdf_micro_verify_manifest()`, behind
  `MCDF_MICRO_ENABLE_INTEGRITY`: every file the manifest lists hashes to its
  recorded digest, and every member the manifest is required to list appears in
  it. SHA-256 is in-tree.

Findings arrive through a callback carrying the kit's own error codes, so a
caller reports what `conformance/errors.md` expects rather than a parallel
vocabulary. Over a sealed `content.md` the binding lives in the policy's
structure attestation (spec §5.2.1), which this reader does not evaluate — so it
reports `E_UNIMPLEMENTED` for that check rather than reporting sections as
missing when they are merely unreadable.

## What it does not do

Signing, key generation and decryption — ever; a reader holds no private keys.
Signature *verification* arrives later as an injected primitive, so the library
owns the format work and the platform supplies the algorithm. Markdown
rendering arrives as an event stream behind `MCDF_MICRO_ENABLE_RENDER`.

A build must never claim a profile whose code is not compiled in — the honest
answer for an unevaluated profile is `E_UNIMPLEMENTED`, never a silent pass.

Path safety is exposed (`mcdf_micro_path_is_safe`) but not enforced at
`open()`: this reader never writes files, and rejecting a container the
reference runtime accepts would be a worse defect than indexing a path a caller
must not extract. Any caller that *does* extract must check it — a failure is
the kit's `E_ASSET_PATH_ESCAPE`.

## Feature gates

Gates are named after the spec's profile ladder, so the build configuration and
the conformance vocabulary are the same vocabulary.

| Option | Adds | Default |
|---|---|---|
| *(baseline)* | USTAR index, member reads, metadata, schema, anchors, Core validation | always |
| `MCDF_MICRO_ENABLE_INTEGRITY` | SHA-256 and `manifest.json` verification | **on** |

Four rules keep them from rotting:

1. **A gate removes code, not just calls.** The `#if` wraps the translation
   unit's contents; a gate that only skipped a call would still drag the object
   into the link.
2. **Declarations stay visible; bodies become stubs** returning
   `MCDF_MICRO_E_DISABLED`, which is what lets one piece of caller code compile
   against every configuration. Callers that branch at compile time test the
   `MCDF_MICRO_HAS_*` macros the headers define.
3. **A build reports what it actually is.** `mcdf_micro_features()` returns a
   bitmask and the CLI declares profiles from it, so an `INTEGRITY=off` build
   reports Integrity `E_UNIMPLEMENTED`.
4. **Gates never change format behaviour.** A Core-only build parses exactly the
   same containers as an all-on build; it just declines to check more. This one
   is checked rather than trusted: the build fails if any file that participates
   in a validity decision includes md4c, so heading anchors cannot quietly start
   being resolved by a parser that only exists in some configurations.

## Building

Part of the main CMake build; nothing to configure.

```sh
cmake -B build -S . && cmake --build build && ctest --test-dir build -R micro
```

Standalone, with no dependencies to fetch:

```sh
cmake -B build -S micro && cmake --build build && ctest --test-dir build
```

Under ESP-IDF the same `CMakeLists.txt` registers a component instead — one
build description, because a second would drift. Nothing in `src/` or
`include/` knows ESP-IDF exists; the adapter that does arrives in
`port/esp-idf/`.

Test targets, all run by `ctest`:

- **`mcdf-micro-tests`** — C99, no framework. Real `.mcdf` archives the
  reference runtime produced and the kit's own conformance vectors (so "reads
  the same bytes" is checked against bytes nobody wrote for the test), plus
  synthetic malformed archives for the cases a conforming packer never emits
  and where an MCU reader gets an over-read: a size field reaching past the
  end, a corrupted header, a missing terminator, a non-octal length.
- **`mcdf-micro-cxx-gate`** — every public header included from a C++23
  translation unit with warnings as errors. Writing the library in C is only
  worth something if C++ can consume it, and `extern "C"` guards rot silently.
- **`mcdf-micro-conformance`** — the published kit, scoring `mcdf-micro-cli`
  exactly as it scores any independent implementation. TAR form only.
- **`*-core`** — the same three against a `MCDF_MICRO_ENABLE_INTEGRITY=OFF`
  build. The classic failure mode of a gated library is the smallest
  configuration quietly ceasing to compile; four builds of a dependency-free C
  library is close to free.

The fuzz harnesses live with the repo's others: `fuzz_micro_tar` walks
untrusted archives, `fuzz_micro_member` feeds raw bytes straight to the member
scanners. Both are C, because what ships is the C99 build.

```sh
./build/micro/mcdf-micro-cli features
./build/micro/mcdf-micro-cli validate path/to/doc.mcdf --profile integrity
./build/micro/mcdf-micro-cli manifest path/to/doc.mcdf
sh conformance/run.sh ./build/micro/mcdf-micro-cli
```

## Status

Core and Integrity are complete, host-buildable and scored by the conformance
kit. Still to come, in order: the Markdown event stream behind
`MCDF_MICRO_ENABLE_RENDER`; the ESP-IDF port under `port/esp-idf/`; signature
verification as an injected primitive.
