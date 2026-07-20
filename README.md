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

## Status

Actively developed. The C++ runtime implements the full document pipeline — the
`mcdf` CLI can `inspect`, `verify`, `sign`, `pack`/`unpack`, `encrypt`/`decrypt`,
`audit`, and `render` (to sanitized HTML + plain text). **MCDF Studio**, a native
Dear ImGui desktop editor, ships alongside it. Remaining work is hardening —
additional signature algorithms, fuzzing, and more export targets.

## Layout

```
spec/          the MCDF specification (CSL-1.0)
conformance/   schemas, test vectors, error taxonomy, runner
include/mcdf/  public library headers
src/           libmcdf implementation (container, crypto, model, serialize, core)
apps/          clients built on libmcdf
  cli/           the `mcdf` command-line client
  studio/        MCDF Studio — the Dear ImGui desktop editor
tests/         unit / determinism / conformance tests
```

## Implementing MCDF in another language

Start with [`conformance/GUIDE.md`](conformance/GUIDE.md), then score yourself:

```sh
./conformance/run.sh /path/to/your-cli
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

## Licensing

Dual-licensed by content type — see [`LICENSING.md`](LICENSING.md):

- **Specification** (`spec/`): Community Specification License 1.0.
- **Code** (everything else): Apache License 2.0.

## Attribution

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

Copyright © 2026 The MCDF Project.
