# MCDF — Markdown Container Document Format

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

**MCDF Studio**, the native desktop editor, shows it all live: edit beside a
Markdown preview while dockable panels track structure binding, per-file
integrity, signatures, encryption, the audit chain, and conformance — type
one character in a signed document and watch the signature break; rebuild and
re-sign and watch it heal.

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
- [x] Import — Markdown, EPUB and HTML (library API, exposed in Studio)
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
- [ ] `mcdf_micro` Signed — signature verification through a platform-supplied
      primitive, so the library keeps zero crypto dependencies. Low priority: a
      constrained reader that reports Core and Integrity honestly is already
      useful, and says `E_UNIMPLEMENTED` for the rest rather than claiming it.

**Engineering**

- [x] Unit, determinism and conformance tests on three operating systems
- [x] Coverage-guided fuzzing over every untrusted-input surface
- [x] A 32-bit ABI build, because every part `mcdf_micro` targets is 32-bit
- [x] Reproducible Docker build; vcpkg overlay port

**Not planned.** OCI as a container backend: the spec has exactly two container
forms, and a registry *transports* the interchange file as a blob — that is
tooling, not a backend, and needs only a registered media type.

## Try it

A complete, signed sample document lives in [`examples/`](examples/):
[`showcase.mcdf`](examples/showcase.mcdf) demonstrates every feature —
structure binding, the integrity manifest, a `did:key` signature, a
hash-chained audit log, and an embedded image — and
[`examples/showcase/`](examples/showcase/) is the same document unpacked, so
you can read every member in your browser: it is just Markdown, YAML, and
JSON. Open the `.mcdf` in MCDF Studio (or `mcdf inspect` it) and follow the
[two-minute tamper demo](examples/README.md).

## Layout

```
spec/          the MCDF specification (CSL-1.0)
conformance/   schemas, test vectors, error taxonomy, runner
examples/      a complete signed sample document (packed + unpacked)
include/mcdf/  public library headers
src/           libmcdf implementation (container, crypto, model, serialize, core)
micro/         mcdf_micro — a portable C99 reader for microcontrollers
               (no allocation, no filesystem, no dependencies)
apps/          clients built on libmcdf
  cli/           the `mcdf` command-line client
  studio/        MCDF Studio — the Dear ImGui desktop editor
                 (core/ is its GUI-free document engine, tested headlessly)
  web/           MCDF Web — the accessible browser client (Svelte 5 + TypeScript)
                 and mcdf-ts, an independent implementation of the format
tests/         unit / determinism / conformance tests
fuzz/          fuzz harnesses + seed corpus for the untrusted-input parsers
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
microcontroller — no allocation, no filesystem, no dependencies, and read-only
forever. It reads the required TAR form through a caller-supplied callback and
keeps its whole index in a caller-supplied arena, so an SD card, a flash
partition and a host `FILE*` are the same three fields. It implements **Core and
Integrity**, scores clean on those vectors, and reports `E_UNIMPLEMENTED` for
the profiles it does not evaluate rather than claiming them; behind a feature
gate it also hands `content.md` to a layout engine as a block/span event stream,
a window at a time, so a reader's RAM is sized by its screen rather than by the
documents it is given. It builds as an
[ESP-IDF component](micro/port/esp-idf/) — for an Xtensa part and a RISC-V one
alike. If your target is constrained, start there rather than from the C++
engine.

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
