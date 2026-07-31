#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Entry point for the `mcdf-ts` CLI. Build the package first (`npm run build`);
// this shim only forwards to the compiled output.
//
// The `.mjs` extension is deliberate: an extensionless bin file is ambiguous to
// Node's module loader, and `npm` installs the friendly name into
// `node_modules/.bin/mcdf-ts` anyway (with the executable bit set, which git on
// Windows would not have preserved).
import { run } from '../dist/cli/main.js';

run(process.argv.slice(2));
