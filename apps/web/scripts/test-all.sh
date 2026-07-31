#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Everything CI runs for the web workspace, in one place so the compose service,
# the Dockerfile and a developer's shell cannot drift apart.
#
#   sh scripts/test-all.sh              # kit at ../../conformance
#   MCDF_KIT=/conformance sh scripts/test-all.sh
set -eu

KIT="${MCDF_KIT:-../../conformance}"

echo '== build mcdf-ts =='
npm run build --workspace packages/mcdf-ts

echo
echo '== type & accessibility checks =='
# svelte-check runs with --fail-on-warnings, so a Svelte a11y warning fails here.
npm run check --workspaces --if-present

echo
echo '== unit tests (includes the conformance vectors) =='
npm test --workspaces --if-present

echo
echo '== license audit =='
node scripts/license-audit.mjs

echo
echo '== conformance kit =='
# mcdf-ts passes every published vector, so any failure here is a regression and
# this block is gated. If a vector for a profile mcdf-ts does not claim is ever
# added, exclude it deliberately rather than letting the whole block fail soft.
#
# Both serializations, because mcdf-ts supports both: `tar` is the REQUIRED
# interchange form (spec 3) and the only one reachable in a browser, `dir` is
# the OPTIONAL authoring form that DirectoryContainer adds under node.
sh "$KIT/run.sh" "$PWD/node_modules/.bin/mcdf-ts" tar
sh "$KIT/run.sh" "$PWD/node_modules/.bin/mcdf-ts" dir

echo
echo 'all checks complete'
