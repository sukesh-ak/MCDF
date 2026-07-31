// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Images referenced from a document live inside the container, not on the web
// origin. This is the end-to-end guard for that: a relative `![](assets/…)` must
// render real pixels in a real browser, and a reference to an absent member must
// say so instead of leaving a blank box.

import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { expect, test } from '@playwright/test';
import {
  MemoryContainer,
  buildManifest,
  manifestToCanonicalJson,
  metadataToYaml,
  schemaToYaml,
} from 'mcdf-ts';

// A 1x1 PNG — enough for the browser to decode and report a natural size.
const PIXEL_PNG =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';

/** Writes a .mcdf to a temp path and returns it. */
function fixture(name: string, content: string, withAsset: boolean): string {
  const c = new MemoryContainer();
  c.writeText('content.md', content);
  c.writeText(
    'metadata.yaml',
    metadataToYaml({
      title: 'Asset Test',
      version: '1.0.0',
      authors: [],
      created_at: '',
      classification: '',
      language: 'en',
    }),
  );
  c.writeText(
    'schema.yaml',
    schemaToYaml({ document_type: 'test', sections: [{ id: 'top', title: 'Top', required: true }] }),
  );
  if (withAsset) c.write('assets/pixel.png', Buffer.from(PIXEL_PNG, 'base64'));
  c.writeText('manifest.json', `${manifestToCanonicalJson(buildManifest(c))}\n`);

  const path = join(mkdtempSync(join(tmpdir(), 'mcdf-e2e-')), name);
  writeFileSync(path, c.toTar());
  return path;
}

/**
 * Playwright cannot drive the native File System Access picker: headless
 * Chromium exposes `showOpenFilePicker` but the dialog never resolves, so the
 * call hangs forever. Removing it makes the app take its `<input type=file>`
 * fallback, which is deterministic and automatable — and is the path Firefox and
 * Safari users get anyway.
 *
 * The consequence, stated plainly: these tests cover the fallback path only. The
 * File System Access path is verified by hand.
 */
test.beforeEach(async ({ page }) => {
  await page.addInitScript(() => {
    Reflect.deleteProperty(window, 'showOpenFilePicker');
    Reflect.deleteProperty(window, 'showSaveFilePicker');
  });
});

/** Opens a document through the app's Open button. */
async function openFixture(page: import('@playwright/test').Page, path: string): Promise<void> {
  const [chooser] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.getByRole('button', { name: 'Open…' }).click(),
  ]);
  await chooser.setFiles(path);
}

test('a container-relative image renders real pixels', async ({ page }) => {
  const path = fixture(
    'with-asset.mcdf',
    '# Top {#top}\n\n![pixel](assets/pixel.png "width=600 align=center")\n',
    true,
  );

  await page.goto('./');
  await openFixture(page, path);

  const img = page.locator('article.rendered img');
  await expect(img).toBeVisible();

  // The whole point: the src must have been rewritten off the page origin.
  await expect(img).toHaveAttribute('src', /^blob:/);
  await expect(img).toHaveAttribute('data-mcdf-member', 'assets/pixel.png');

  // Rewritten is not the same as rendered — confirm the browser decoded it.
  await expect
    .poll(async () => img.evaluate((el: HTMLImageElement) => el.naturalWidth))
    .toBeGreaterThan(0);
});

test('a referenced asset that is not in the container is called out', async ({ page }) => {
  const path = fixture('no-asset.mcdf', '# Top {#top}\n\n![gone](assets/gone.png)\n', false);

  await page.goto('./');
  await openFixture(page, path);

  await expect(page.locator('article.rendered')).toContainText('Missing asset: assets/gone.png');
  await expect(page.locator('article.rendered img')).toHaveCount(0);
  await expect(page.getByRole('status')).toContainText('referenced but not in this container');
});

test('attaching an asset makes an existing reference resolve', async ({ page }) => {
  await page.goto('./');

  // Reference an asset that does not exist yet.
  const editor = page.getByRole('textbox', { name: 'Document source, CommonMark' });
  await editor.click();
  await page.keyboard.press('Control+End');
  await page.keyboard.type('\n\n![pixel](assets/pixel.png)');
  await expect(page.locator('article.rendered')).toContainText('Missing asset: assets/pixel.png');

  // Attach it, and the same reference resolves.
  const [chooser] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.getByRole('button', { name: 'Attach asset…' }).click(),
  ]);
  const assetPath = join(mkdtempSync(join(tmpdir(), 'mcdf-e2e-')), 'pixel.png');
  writeFileSync(assetPath, Buffer.from(PIXEL_PNG, 'base64'));
  await chooser.setFiles(assetPath);

  await expect(page.locator('article.rendered').getByText('Missing asset')).toHaveCount(0);
  await expect(page.locator('article.rendered img').first()).toHaveAttribute('src', /^blob:/);
});
