// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { defineConfig, devices } from '@playwright/test';

const PORT = 8080;
const BASE = '/MCDF/';

export default defineConfig({
  testDir: './e2e',
  fullyParallel: true,
  forbidOnly: process.env['CI'] !== undefined,
  retries: process.env['CI'] !== undefined ? 1 : 0,
  reporter: process.env['CI'] !== undefined ? 'github' : 'list',
  use: {
    baseURL: `http://127.0.0.1:${PORT}${BASE}`,
    trace: 'on-first-retry',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  // Tests run against the production build, so what CI checks is what ships.
  webServer: {
    command: 'npm run build && npm run preview',
    url: `http://127.0.0.1:${PORT}${BASE}`,
    reuseExistingServer: process.env['CI'] === undefined,
    timeout: 180_000,
  },
});
