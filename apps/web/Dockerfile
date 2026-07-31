# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Reproducible build/test/serve image for the web workspace, mirroring the C++
# side's Dockerfile-as-CI-path habit. Independent of the C++ build: this image
# never compiles anything native.
#
#   docker build -f apps/web/Dockerfile -t mcdf-web .        # from the repo root
#   docker run --rm -p 8080:8080 mcdf-web                    # serve the built app
#   docker build -f apps/web/Dockerfile --target test .       # build + run all tests
#
# The repo root is the build context because the conformance kit lives outside
# apps/web and IS the test suite for mcdf-ts.

FROM node:22-bookworm-slim AS deps
WORKDIR /app
# Manifests first so the dependency layer is cached independently of source.
COPY apps/web/package.json apps/web/package-lock.json ./
COPY apps/web/packages/mcdf-ts/package.json ./packages/mcdf-ts/
COPY apps/web/packages/app/package.json ./packages/app/
RUN npm ci --no-audit --no-fund

FROM deps AS build
COPY apps/web/ ./
# `npm ci` ran before the workspace sources existed, so it had no bin file to
# link for the mcdf-ts CLI. Link it now rather than reinstalling: the conformance
# kit invokes the CLI by path and needs it executable.
RUN chmod +x packages/mcdf-ts/bin/mcdf-ts.mjs \
 && ln -sf /app/packages/mcdf-ts/bin/mcdf-ts.mjs node_modules/.bin/mcdf-ts
RUN npm run build --workspace packages/mcdf-ts \
 && npm run build --workspace packages/app

# Unit tests, the conformance kit, type/a11y checks and the license gate.
FROM build AS test
COPY conformance/ /conformance/
RUN MCDF_KIT=/conformance sh scripts/test-all.sh

# Static serving. `vite preview` honours the configured base path, so the
# container reproduces the GitHub Pages URL layout exactly (/MCDF/).
FROM build AS serve
EXPOSE 8080
CMD ["npx", "vite", "preview", "--host", "0.0.0.0", "--port", "8080", "--strictPort"]
