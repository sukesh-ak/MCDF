// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { defineConfig } from 'vitest/config';

import { mcdfPwa } from './vite-pwa.ts';

// GitHub Pages serves a project site from `/<repo>/`, so the app is built with
// that base path. Set MCDF_BASE=/ when deploying to a custom
// domain or serving from a root.
const base = process.env['MCDF_BASE'] ?? '/MCDF/';

export default defineConfig({
  base,
  plugins: [svelte(), mcdfPwa()],
  build: {
    target: 'es2022',
    sourcemap: true,
    // CodeMirror is one indivisible library and lands in its own lazily-loaded
    // chunk, so the default 500 kB warning fires on something already handled.
    // Raised rather than silenced, so a *new* oversized chunk still shows up.
    chunkSizeWarningLimit: 600,
    rollupOptions: {
      output: {
        // Split the large, slow-moving libraries away from app code. Two
        // reasons: the editor and the diff view load on demand rather than
        // blocking first paint, and an app update re-downloads only the app
        // chunk instead of invalidating every megabyte in the offline cache.
        manualChunks(id) {
          if (!id.includes('node_modules')) return undefined;
          if (id.includes('@codemirror/merge')) return 'diff';
          if (id.includes('@codemirror') || id.includes('@lezer')) return 'editor';
          if (id.includes('micromark') || id.includes('mdast') || id.includes('unist')) {
            return 'markdown';
          }
          if (id.includes('@noble') || id.includes('@hpke')) return 'crypto';
          return undefined;
        },
      },
    },
  },
  server: {
    // Bind on all interfaces so `npm run dev` works from inside a container.
    host: true,
    port: 5173,
    watch: {
      // Docker bind mounts on Windows and macOS do not deliver inotify events
      // into the container, so the watcher sees nothing and hot reload silently
      // stops working — the page keeps serving stale modules with no error.
      // Polling is the only thing that works there; it stays opt-in so native
      // runs keep the cheaper native watcher.
      usePolling: process.env['MCDF_POLL'] === '1',
      interval: 300,
    },
  },
  test: {
    // Node by default: the store and mcdf-ts are DOM-free, and spinning up jsdom
    // for them costs more than the tests themselves. Component tests opt in with
    // a `@vitest-environment jsdom` comment.
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
});
