# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
#
# Multi-stage build for the MCDF C++ runtime.
#   base  -> toolchain (C++23) + vcpkg
#   deps  -> installs vcpkg dependencies (cached unless vcpkg.json changes)
#   build -> configure + compile + run tests
#   runtime -> slim image containing just the `mcdf` CLI
#
# Usage:
#   docker build -t mcdf .            # builds deps, compiles, runs tests
#   docker run --rm mcdf --version    # run the CLI
#
#   # Build the MCDF Studio GUI editor (Linux binary) and extract it to ./build:
#   scripts/build-studio-docker.sh
#   # (or manually: docker build --target studio -t mcdf-studio-build . )

# ---- base: toolchain + vcpkg ----
FROM ubuntu:24.04 AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential g++ cmake ninja-build git curl zip unzip tar \
      pkg-config ca-certificates perl \
    && rm -rf /var/lib/apt/lists/*

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"
RUN git clone --depth 1 https://github.com/microsoft/vcpkg "${VCPKG_ROOT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
WORKDIR /src

# ---- deps: install dependencies from the manifest (cache layer) ----
# Only the manifest is copied so this layer is reused until vcpkg.json changes.
FROM base AS deps
COPY vcpkg.json ./
RUN vcpkg install --x-feature=tests --clean-after-build

# ---- build: compile + test ----
FROM deps AS build
COPY . .
RUN cmake --preset default \
    && cmake --build --preset default \
    && ctest --preset default

# ---- studio: build the MCDF Studio GUI editor (Dear ImGui) ----
# A GUI client, built on demand (`--target studio`) - not part of the default
# runtime image. vcpkg's glfw3 needs X11 / Wayland / OpenGL development
# libraries present before it is compiled.
FROM deps AS studio
RUN apt-get update && apt-get install -y --no-install-recommends \
      xorg-dev libxkbcommon-dev libwayland-dev wayland-protocols \
      extra-cmake-modules libgl1-mesa-dev libglu1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*
# Add the Studio feature deps (imgui + glfw3) on top of the cached base deps.
RUN vcpkg install --x-feature=tests --x-feature=studio --clean-after-build
COPY . .
RUN cmake --preset studio && cmake --build --preset studio
# Result: /src/build/studio/apps/studio/mcdf-studio  (Linux/ELF GUI binary)

# ---- runtime: slim image with the CLI only ----
# The default x64-linux vcpkg triplet links statically, so the binary needs no
# vcpkg runtime libraries; ca-certificates is kept for future TLS use.
FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/default/apps/cli/mcdf /usr/local/bin/mcdf
ENTRYPOINT ["mcdf"]
CMD ["--help"]
