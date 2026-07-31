// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

// Accessibility warnings are the reason this client picked Svelte
// §9.2): the compiler flags missing labels, bad ARIA and handlers on
// non-interactive elements at build time. Nothing is filtered out here, and
// `npm run check` runs svelte-check with --fail-on-warnings so an inaccessible
// component cannot pass CI.
export default {
  preprocess: vitePreprocess(),
};
