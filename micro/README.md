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

The YAML and JSON readers are deliberately not general parsers. They walk the
one shape the spec gives each file and refuse anything else rather than
guessing — a reader does not need a conforming YAML parser to show a title, and
putting one on a 2 MB part to get there would be the wrong trade. Both stream
through a 64-byte window, so nothing scales with member size.

Documented limits: a `metadata.yaml` line may not exceed
`MCDF_MICRO_LINE_MAX` (256) bytes and a `manifest.json` scalar may not exceed
`MCDF_MICRO_VALUE_MAX` (128); over that, the value is reported `E_RANGE` rather
than silently truncated. A value that simply does not fit the caller's buffer
is also `E_RANGE`, with `*out_len` set to the length it needed.

## What it does not do

Anything requiring cryptography. Hashing members and comparing them to the
manifest (Integrity) and verifying signatures (Signed) arrive behind their own
feature gates; signing, key generation and decryption never do, because a
reader holds no private keys. Markdown rendering arrives as an event stream
behind `MCDF_MICRO_ENABLE_RENDER`.

Until then `mcdf_micro_features()` returns `MCDF_MICRO_FEATURE_CORE` alone. A
build must never claim a profile whose code is not compiled in — the honest
answer for an unevaluated profile is `E_UNIMPLEMENTED`, never a silent pass.

Path safety is exposed (`mcdf_micro_path_is_safe`) but not enforced at
`open()`: this reader never writes files, and rejecting a container the
reference runtime accepts would be a worse defect than indexing a path a caller
must not extract. Any caller that *does* extract must check it — a failure is
the kit's `E_ASSET_PATH_ESCAPE`.

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

Two test targets, both run by `ctest`:

- **`mcdf-micro-tests`** — C99, no framework. Real `.mcdf` archives the
  reference runtime produced (so "reads the same bytes" is checked against
  bytes nobody wrote for the test), plus synthetic malformed archives for the
  cases a conforming packer never emits and where an MCU reader gets an
  over-read: a size field reaching past the end, a corrupted header, a missing
  terminator, a non-octal length.
- **`mcdf-micro-cxx-gate`** — every public header included from a C++23
  translation unit with warnings as errors. Writing the library in C is only
  worth something if C++ can consume it, and `extern "C"` guards rot silently.

## Status

The container reader is complete and host-buildable, covered by the suites
above. Still to come, in order: the CLI the conformance kit scores and SHA-256
behind `MCDF_MICRO_ENABLE_INTEGRITY`; the Markdown event stream behind
`MCDF_MICRO_ENABLE_RENDER`; the ESP-IDF port under `port/esp-idf/`.
