# MCDF — Markdown Container Document Format

Make working with documents easier for everyone. MCDF treats a document as a
structured, versionable, signable, AI-native **container** — plain Markdown +
YAML + JSON + assets — instead of a fixed-layout, print-first file.

This repository holds the **specification** and the **primary C++ runtime**.

- **Spec:** [`spec/`](spec/) — the format definition (RFC).
- **Runtime:** the C++ library `libmcdf` (`src/`, `include/`) and clients (`apps/`).
- **Open by design:** `.mcdf` is re-implementable in any language from the spec
  and the conformance kit; the C++ runtime is the canonical one, not a gate.

## Status

Early development. The format is specified; the runtime is being built in phases
(read/inspect → integrity → signing → packaging → encryption → audit → render).

## Layout

```
spec/          the MCDF specification (CSL-1.0)
include/mcdf/  public library headers
src/           libmcdf implementation (the engine)
apps/          clients built on libmcdf
  cli/           the `mcdf` command-line client
tests/         unit / determinism / conformance tests
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

> To pin dependency versions reproducibly, run
> `vcpkg x-update-baseline --add-initial-baseline` once and commit the result.

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

All third-party components are **MIT or Apache-2.0**: OpenSSL, nlohmann-json,
yaml-cpp, md4c, spdlog, cxxopts, and doctest (tests only). New dependencies are
added only after review.

## Licensing

Dual-licensed by content type — see [`LICENSING.md`](LICENSING.md):

- **Specification** (`spec/`): Community Specification License 1.0.
- **Code** (everything else): Apache License 2.0.

Copyright © 2026 The MCDF Project.
