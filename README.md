# MCDF — Markdown Container Document Format

[![CI](https://img.shields.io/github/actions/workflow/status/sukesh-ak/MCDF/ci.yml?branch=main&label=CI)](https://github.com/sukesh-ak/MCDF/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/sukesh-ak/MCDF?label=release)](https://github.com/sukesh-ak/MCDF/releases)
[![Code licence](https://img.shields.io/badge/code-Apache--2.0-blue)](LICENSE)
[![Spec licence](https://img.shields.io/badge/spec-CSL--1.0-blue)](spec/LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-f34b7d)](CMakeLists.txt)
[![Platforms](https://img.shields.io/badge/desktop-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](#build)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-e7352c)](micro/port/esp-idf/)

Make working with documents easier for everyone. MCDF treats a document as a
structured, versionable, signable, AI-native **container** — plain Markdown +
YAML + JSON + assets — instead of a fixed-layout, print-first file.

This repository holds the **specification** and the **primary C++ runtime**.

- **Spec:** [`spec/`](spec/) — the format definition (RFC).
- **Runtime:** the C++ library `libmcdf` (`src/`, `include/`) and clients (`apps/`).
- **Conformance kit:** [`conformance/`](conformance/) — JSON Schemas, known-answer
  test vectors, an error taxonomy, and a runner that scores *any* implementation.
- **Open by design:** `.mcdf` is re-implementable in any language from the spec
  and the conformance kit; the C++ runtime is the canonical one, not a gate.
  Reading, writing and modifying a document needs **no cryptography** — a
  container is just Markdown + YAML + JSON.

Created and maintained by **[Sukesh Ashok Kumar](https://github.com/sukesh-ak)** —
the format design, the specification, and the reference implementation.

## Status

Actively developed; prebuilt CLI and Studio binaries for Windows, Linux and
macOS are on the [releases page](https://github.com/sukesh-ak/MCDF/releases).

The C++ runtime implements the full document pipeline — the `mcdf` CLI can
`inspect`, `validate` (all five conformance profiles), `verify`, `sign`
(Ed25519 / ECDSA P-256 via `did:key`), `pack`/`unpack`, `encrypt`/`decrypt`
(AES-256-GCM + HPKE), `audit`, and `render` (sanitized HTML + plain text with
a provenance stamp).

**MCDF Studio**, the desktop editor, shows all of that live: you write next to a
Markdown preview, with dockable panels tracking structure, per-file integrity,
signatures, encryption, the audit chain and conformance as you type.

Hardening is continuous: unit + determinism tests on three OSes, known-answer
conformance vectors, and coverage-guided fuzzing (libFuzzer + ASan) over
every surface that parses untrusted input.

### Feature status

**Format**

- [x] Specification (RFC) and conformance kit — schemas, known-answer vectors,
      error taxonomy, a runner that scores any implementation
- [x] Container serialization — TAR (required) and directory (optional)
- [x] All five profiles — Core, Integrity, Signed, Encrypted, Render

**C++ runtime and CLI**

- [x] `inspect` · `validate` · `verify` · `pack` / `unpack` · `audit`
- [x] `sign` — Ed25519 and ECDSA P-256 via `did:key`
- [x] `encrypt` / `decrypt` — AES-256-GCM + HPKE
- [x] `render` — canonical HTML and plain text with a provenance stamp
- [x] Import — bring in Markdown, EPUB or HTML (from Studio, or the library)
- [ ] RSA-PSS signatures
- [ ] DOCX / PDF export
- [ ] PDF import — a separate tool consuming `libmcdf`

**Clients**

- [x] `mcdf` CLI — Windows, Linux, macOS
- [x] MCDF Studio — the Dear ImGui desktop editor
- [x] MCDF Web — accessible browser client, installable PWA

**Independent implementations**

- [x] `mcdf-ts` (TypeScript) — every published vector, all five profiles
- [x] `mcdf_micro` (C99) — Core, Integrity, and the Render event stream
- [x] `mcdf_micro` as an ESP-IDF component — esp32s3 (Xtensa) and esp32p4
      (RISC-V), every feature-gate configuration, warnings as errors
- [ ] `mcdf_micro` Signed — checking signatures on the device itself. Low
      priority: a small reader that does Core and Integrity properly, and admits
      it has not checked the rest, is useful as it stands. If you need the trust
      badge today, verify the file on a real computer before it ever reaches the
      device.

**Engineering**

- [x] Unit, determinism and conformance tests on three operating systems
- [x] Coverage-guided fuzzing over every untrusted-input surface
- [x] A 32-bit ABI build, because every part `mcdf_micro` targets is 32-bit
- [x] Reproducible Docker build; vcpkg overlay port

**Not planned:** OCI as a container backend. You can already push a `.mcdf` to a
registry today — it travels as an ordinary blob, like any other file. That is a
job for `oras push`, not a reason for the format to grow a third shape.

## What's actually inside one

A `.mcdf` file is a tar archive. Unpack it and there is nothing mysterious in
there — you can read the whole thing in a text editor:

```
showcase.mcdf
├── content.md          the document, in ordinary Markdown
├── metadata.yaml       title, authors, dates
├── schema.yaml         which sections the document promises to have
├── manifest.json       a SHA-256 for each file it covers (not itself, the
│                       log, or the signatures — those change afterwards)
├── signatures/
│   └── mcdf-project.sig    detached signature, signer named by did:key
├── audit.log           who changed what, hash-chained so edits show
├── audit.checkpoint    a signed point in that history
└── assets/
    └── mcdf-logo.png   images and anything else the document carries
```

`content.md` is the document. Everything else describes it or protects it, and
you add each layer when you need it: hashes to tell whether a file changed, a
signature to tell who wrote it, an encryption policy to keep parts of it sealed.
That laddering is exactly what the five profiles name — how far up a document
has climbed, and how far up a given tool can check. A reader on a microcontroller
can do the first two rungs and say so honestly; it doesn't have to pretend to do
the rest.

## Try it

[`showcase.mcdf`](examples/showcase.mcdf) in [`examples/`](examples/) is the
document above, signed and complete. [`examples/showcase/`](examples/showcase/)
is the same thing unpacked, so you can click through it on GitHub without
installing anything.

Then try breaking it. Open it in MCDF Studio (or run `mcdf inspect`), change one
character of the text, and watch the signature go red; rebuild and re-sign and
watch it come back. That is the [two-minute tamper demo](examples/README.md),
and it is the fastest way to see the point of the whole format.

## Layout

```
MCDF/
├── spec/            the specification itself (CSL-1.0)
├── conformance/     schemas, test vectors, error taxonomy, and a runner that
│                    scores any implementation, including yours
├── examples/        the signed sample document, packed and unpacked
├── include/mcdf/    public headers for libmcdf
├── src/             libmcdf itself — container, crypto, model, serialize, core
├── micro/           mcdf_micro, the C99 reader for microcontrollers
│   └── port/esp-idf/    its ESP-IDF component and a runnable example
├── apps/
│   ├── cli/         the `mcdf` command
│   ├── studio/      MCDF Studio, the desktop editor (core/ is its document
│   │                engine with no GUI attached, so it can be tested headlessly)
│   └── web/         MCDF Web, the browser client — and mcdf-ts, a second
│                    implementation of the format written from the spec alone
├── tests/           unit, determinism and conformance tests
└── fuzz/            fuzz harnesses and seed corpus for the parsers that read
                     untrusted input
```

## Implementing MCDF in another language

Start with [`conformance/GUIDE.md`](conformance/GUIDE.md), then score yourself:

```sh
./conformance/run.sh /path/to/your-cli
```

**It has been done.** [`mcdf-ts`](apps/web/packages/mcdf-ts) is a TypeScript
implementation written from the specification and this kit alone — no bindings
to `libmcdf`, no WASM build of it. It **passes every published vector across all
five profiles**, and its canonical manifest and canonical renders are
byte-identical to the C++ CLI's — produced by a different CommonMark parser
entirely. The signatures in the signed and encrypted vectors were produced by
`libmcdf` and verify there with no shared code, which is the claim made
checkable. It is both a worked example for your own port and an npm library you
can build on.

**Twice, in fact.** [`mcdf_micro`](micro) is a **C99** reader small enough for a
microcontroller: no allocation, no filesystem, no dependencies, and read-only
forever. Bytes reach it through a callback you write, and its whole index lives
in an arena you hand it, so an SD card, a flash partition and a plain `FILE*`
all look the same from inside.

It checks **Core and Integrity**, and answers `E_UNIMPLEMENTED` for the profiles
it does not check rather than pretending. Turn on the render gate and it will
feed `content.md` to your layout engine as a stream of block and span events, a
screenful at a time — so the RAM you need depends on your display, not on how
long a book someone hands you. It builds as an
[ESP-IDF component](micro/port/esp-idf/) for both Xtensa and RISC-V parts. If
your target is small, start here rather than with the C++ engine.

## MCDF Web — the accessible client

[`apps/web`](apps/web) is the browser client, and it exists for a specific
reason: MCDF Studio is built on Dear ImGui, which draws its own widgets and
cannot talk to a screen reader. Accessible authoring belongs where accessibility
is native, so it lives on the web — real DOM, semantic HTML, keyboard-first,
WCAG 2.2 AA (see [ACCESSIBILITY.md](apps/web/ACCESSIBILITY.md)). Documents are
parsed, hashed and verified in the tab; nothing is uploaded.

It covers the same profiles the CLI does — authoring, integrity, signing with
Ed25519 or ES256, HPKE encryption, the audit chain and a diff view — and it is
an installable PWA that keeps working with the network switched off.

```sh
cd apps/web
docker compose run --rm install
docker compose up dev            # http://localhost:5173/MCDF/
```

## Build

Builds on **Windows, Linux, and macOS** — anywhere a C++23 toolchain and vcpkg
are available. Requires a C++23 compiler (recent MSVC / Clang / GCC),
[CMake](https://cmake.org) ≥ 3.21, [Ninja](https://ninja-build.org), and
[vcpkg](https://vcpkg.io) (set `VCPKG_ROOT`). Dependencies are declared in
[`vcpkg.json`](vcpkg.json) and fetched automatically.

```sh
cmake --preset default      # configures + installs deps via vcpkg
cmake --build --preset default
ctest --preset default      # run tests
./build/default/apps/cli/mcdf --version
```

**MCDF Studio** (the GUI editor) builds behind the optional `studio` feature,
which pulls in Dear ImGui, GLFW and stb:

```sh
cmake --preset studio
cmake --build --preset studio --target mcdf-studio
```

### Using libmcdf in your project

Both routes provide `find_package(mcdf CONFIG)` and the `mcdf::mcdf` target
(external tools like the planned PDF importer consume the library this way):

**vcpkg overlay port** — point vcpkg at this repo's [`ports/`](ports/):

```sh
vcpkg install mcdf --overlay-ports=<MCDF checkout>/ports
```

or add that directory to `"overlay-ports"` in your project's
`vcpkg-configuration.json`. The port pins a tested commit and is updated
each release.

**CMake FetchContent** — the subproject builds only the library:

```cmake
include(FetchContent)
FetchContent_Declare(mcdf
  GIT_REPOSITORY https://github.com/sukesh-ak/MCDF
  GIT_TAG v0.6.0)   # any release tag from v0.5.1 onwards
FetchContent_MakeAvailable(mcdf)
target_link_libraries(app PRIVATE mcdf::mcdf)
```

Either way your project must be able to find libmcdf's dependencies
(openssl, nlohmann-json, yaml-cpp, md4c, spdlog) — with vcpkg they resolve
automatically; with FetchContent declare them in your own manifest.

### Docker (reproducible build, used for CI/CD)

A multi-stage [`Dockerfile`](Dockerfile) provides a hermetic Linux build that
compiles and runs the tests, then produces a slim image with just the CLI:

```sh
docker build -t mcdf .            # builds deps, compiles, runs tests
docker run --rm mcdf --version    # run the CLI from the runtime image
```

Dependency builds are cached in their own layer, so rebuilds are fast unless
`vcpkg.json` changes. Native builds on Windows/macOS/Linux use the CMake + vcpkg
flow above; Docker gives the hermetic environment CI/CD will reuse.

## Dependencies

All third-party components are permissive (**MIT or Apache-2.0**), declared in
[`vcpkg.json`](vcpkg.json); new ones are added only after review. Runtime + CLI:

- **OpenSSL** (Apache-2.0) — SHA-256, Ed25519 / ECDSA signing, encryption
- **nlohmann-json** (MIT) — `manifest.json` and other JSON
- **yaml-cpp** (MIT) — `schema.yaml` / `metadata.yaml`
- **md4c** (MIT) — CommonMark (Markdown) parsing
- **spdlog** (MIT) — logging
- **cxxopts** (MIT) — CLI argument parsing
- **doctest** (MIT) — unit tests (test builds only)

MCDF Studio, the optional GUI editor (`apps/studio`, `studio` feature), adds:

- **Dear ImGui** (MIT) — GUI toolkit (docking + GLFW/OpenGL3 backends)
- **GLFW** (zlib) — window and input
- **stb_image** (MIT / public domain) — decode images for the live preview

`mcdf_micro` has **no package manager to resolve anything**, so the one
dependency it has is vendored instead:

- **md4c** (MIT) — [`micro/render/md4c/`](micro/render/md4c/), an unmodified copy
  of `release-0.5.3` with its licence and upstream hashes recorded alongside.
  Two files, needed only when the render gate is on. It is the same parser the
  C++ runtime takes from vcpkg; there is no vcpkg under ESP-IDF.

## Licensing

Dual-licensed by content type — see [`LICENSING.md`](LICENSING.md):

- **Specification** (`spec/`): Community Specification License 1.0.
- **Code** (everything else): Apache License 2.0.

## Attribution

MCDF — the format, the specification, the `libmcdf` runtime, the CLI, MCDF Studio
and the web client — was created by **Sukesh Ashok Kumar**
([@sukesh-ak](https://github.com/sukesh-ak)), who maintains the project.

### Third-party components

MCDF Studio (the GUI editor) bundles these third-party components, each under its
own license and kept intact in-tree:

- **imgui_md** — Dmitry Mekhontsev — MIT — live Markdown preview
- **ImGuiColorTextEdit** — goossens fork — MIT — source-editor widget
- **IconFontCppHeaders** (`IconsFontAwesome6.h`) — zlib — Font Awesome icon names

Bundled fonts (shipped as assets, not linked code):

- **Roboto** and **Roboto Mono** — Apache-2.0
- **Font Awesome 6 Free** (Solid glyphs) — SIL OFL 1.1
- **DejaVu Sans Mono** — DejaVu Fonts License (Bitstream Vera derivative)

The `imfd` file dialog (`apps/studio/include/imfiledialog/`) is first-party MCDF
code (Apache-2.0). Thanks to all the upstream authors.

Copyright © 2026 Sukesh Ashok Kumar and The MCDF Project.
