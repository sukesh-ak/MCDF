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

# ---- runtime: slim image with the CLI only ----
# The default x64-linux vcpkg triplet links statically, so the binary needs no
# vcpkg runtime libraries; ca-certificates is kept for future TLS use.
FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/default/apps/cli/mcdf /usr/local/bin/mcdf
ENTRYPOINT ["mcdf"]
CMD ["--help"]
