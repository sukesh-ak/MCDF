<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# `mcdf_micro` under ESP-IDF

Two components and an example. Nothing in `micro/src/` or `micro/include/`
knows ESP-IDF exists; everything that does is here.

| Component | What it is |
|---|---|
| `mcdf_micro` | The portable reader, registered as a component. Carries no sources of its own — `micro/CMakeLists.txt` is the one build description, and this directory exists to give the component a name. |
| `mcdf_micro_idf` | The adapter: binds the reader's byte-source callback to a file on a mounted VFS or to a raw flash partition. |

The name is deliberate. ESP-IDF takes a component's name from its directory and
those names share one flat namespace across a whole firmware image, so the
component is not called `micro` — it is spelled the way the include path
(`<mcdf_micro/mcdf_micro.h>`), the symbol prefix and the registry entry are
spelled.

## The adapter

```c
#include "mcdf_micro_idf.h"

static uint8_t arena[MCDF_MICRO_ARENA_SIZE(64, 2048)];

mcdf_micro_idf_file file = MCDF_MICRO_IDF_FILE_INIT;   /* not {0} - 0 is stdin */
mcdf_micro_source   src;
mcdf_micro_reader  *reader;

ESP_ERROR_CHECK(mcdf_micro_idf_file_open(&file, "/sdcard/book.mcdf", &src));
if (mcdf_micro_open(&src, arena, sizeof arena, &reader) == MCDF_MICRO_OK) {
  /* ... */
  mcdf_micro_close(reader);
}
mcdf_micro_idf_file_close(&file);
```

A flash partition is the same three lines with
`mcdf_micro_idf_partition_open()`, which is what a device shipping with its
documents baked in wants — no filesystem at all.

**It allocates nothing**, because the core does not either: the handle you pass
in is the storage, and it can be static or live on a stack frame that outlives
the reader. That last part is the one rule that bites — the reader holds the
source by pointer and reads through it lazily, so closing the file underneath it
is a use-after-close rather than a no-op.

**`esp_err_t` out, `mcdf_micro_status` back.** The boundary speaks both
vocabularies on purpose. "The SD card is not mounted" and "this document has an
unbound schema section" are not the same kind of fact, and folding them into one
error space would lose which of them happened.

**There is no memory-mapped source**, and that is a decision rather than an
omission. `esp_partition_mmap()` would buy zero-copy access, but the reader's
source contract is a *copy* into the caller's buffer — so a mapped source would
still `memcpy`, and it would spend MMU address space to do it. What it saves
over `esp_partition_read()` is the driver call, which is not what a document
reader is bounded by.

## The example

```sh
cd micro/port/esp-idf/example
idf.py set-target esp32s3      # or esp32p4, or any other supported part
idf.py build flash monitor
```

It opens a container from the `mcdf` partition, prints the title and author,
validates at Core, verifies at Integrity, and walks `content.md` as a block/span
event stream — reporting for each what the build actually compiled, so a
Core-only build says `E_DISABLED` rather than quietly claiming a pass.

Put a document in the partition with the container the repository ships:

```sh
parttool.py --port /dev/ttyUSB0 write_partition \
  --partition-name=mcdf --input ../../../../examples/showcase.mcdf
```

Build one gate configuration at a time by turning the gates off:

```sh
idf.py -D MCDF_MICRO_ENABLE_RENDER=OFF -D MCDF_MICRO_ENABLE_INTEGRITY=OFF build
```

The example calls every gated entry point unconditionally, which is the pattern
to copy: a disabled feature keeps its declaration and returns
`MCDF_MICRO_E_DISABLED`, so one piece of caller code compiles against every
configuration. Only the render *buffer* is worth removing by hand, because it is
the one place a gate costs RAM rather than just an answer.

## What is proved, and where

CI compiles this for **esp32s3 and esp32p4**, in all four gate configurations,
with warnings as errors. Both parts, not one: the S3 is Xtensa and the P4 is
RISC-V, so a clean build of one says nothing about the other.

That job proves the code compiles and links under the vendor toolchain's warning
set, which is stricter than the host matrix in the places that bite embedded C.
It proves nothing about behaviour on silicon. Behaviour is covered from the
other side: every parser, both profiles, the event stream and the windowed walk
are architecture-independent and are executed by the host test matrix, the
sanitizer and 32-bit jobs, and the fuzzers. What is left — flash and SD reads
through this adapter, timing, memory pressure — is what a real board is for, and
is checked by hand on one.

There is no emulator in that list on purpose. No single simulator covers both
parts — the QEMU fork does not do the P4, and the RISC-V emulator that does
cannot run an S3 — so covering this pair on-simulator would mean adopting two of
them, for a library with no packed structs, no type punning and an explicitly
aligned arena.

## Using it in your own project

Until the components are published to the ESP Component Registry, point at them
from your project's top-level `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/MCDF/micro/port/esp-idf/components")
```

then `REQUIRES mcdf_micro mcdf_micro_idf` in the component that reads documents.
`mcdf_micro_idf` is optional — a project that already has its bytes in RAM, or
that reads them over a transport of its own, needs only `mcdf_micro` and a
five-line read callback.
