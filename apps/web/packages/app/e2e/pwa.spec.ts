// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// "Fully offline" is a claim, so it is tested by actually cutting the network.
// Everything below runs against the production build, which is the only place
// the service worker exists — in dev it deliberately unregisters itself.

import { expect, test } from '@playwright/test';

/** Resolves once a worker has installed and taken control of the page. */
async function waitForController(page: import('@playwright/test').Page): Promise<void> {
  await page.waitForFunction(
    () => navigator.serviceWorker?.controller !== null,
    undefined,
    { timeout: 20_000 },
  );
}

test.describe('offline', () => {
  test('the app loads and works with the network cut', async ({ page, context }) => {
    await page.goto('./');
    await expect(page.getByRole('heading', { name: 'MCDF Web', level: 1 })).toBeVisible();
    await waitForController(page);

    // Give the precache a moment to finish before pulling the plug.
    await page.waitForTimeout(500);
    await context.setOffline(true);
    await page.reload();

    await expect(page.getByRole('heading', { name: 'MCDF Web', level: 1 })).toBeVisible();
    // Not just the shell: the editor is a lazily-loaded chunk, so its presence
    // proves the precache covered the split bundles and not only index.html.
    await expect(page.getByRole('textbox', { name: 'Document source, CommonMark' })).toBeVisible();

    // And the document machinery still runs — this is a local-first app, so
    // offline is not a degraded mode.
    await page.getByRole('button', { name: /Add manifest/ }).click();
    await expect(page.getByText('up to date')).toBeVisible();

    await context.setOffline(false);
  });

  test('the web app manifest is installable', async ({ request }) => {
    const response = await request.get('./manifest.webmanifest');
    expect(response.ok()).toBe(true);

    const manifest = (await response.json()) as {
      name: string;
      start_url: string;
      scope: string;
      display: string;
      icons: { src: string; sizes: string }[];
    };
    expect(manifest.name).toBe('MCDF Web');
    expect(manifest.display).toBe('standalone');
    // Scope and start_url must carry the Pages base path, or an installed app
    // opens at the domain root and 404s.
    expect(manifest.start_url).toBe('/MCDF/');
    expect(manifest.scope).toBe('/MCDF/');
    expect(manifest.icons.length).toBeGreaterThan(0);
    expect(manifest.icons[0]?.src).toBe('/MCDF/icon.svg');
  });
});
