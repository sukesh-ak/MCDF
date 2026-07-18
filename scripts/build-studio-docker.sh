#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
#
# Build MCDF Studio (the Dear ImGui GUI editor) inside Docker and copy the
# resulting Linux binary to ./build/studio/apps/studio/ on the host.
#
# The binary is a Linux/ELF build (the Docker toolchain is Linux). To run the
# GUI you need a Linux environment with a display (e.g. WSLg or an X server).
set -euo pipefail

cd "$(dirname "$0")/.."

docker build --target studio -t mcdf-studio-build .

id="$(docker create mcdf-studio-build)"
trap 'docker rm -f "$id" >/dev/null 2>&1 || true' EXIT

dest="build/studio/apps/studio"
mkdir -p "$dest"
docker cp "$id:/src/build/studio/apps/studio/mcdf-studio" "$dest/mcdf-studio"

echo "Copied -> $dest/mcdf-studio"
ls -la "$dest/mcdf-studio"
