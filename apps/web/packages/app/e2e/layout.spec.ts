// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Layout regressions are easy to reintroduce and invisible to unit tests, so
// they are measured here rather than eyeballed. Horizontal overflow is also a
// WCAG 2.2 reflow concern (SC 1.4.10), not only an aesthetic one.

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

test.beforeEach(async ({ page }) => {
  await page.addInitScript(() => {
    Reflect.deleteProperty(window, 'showOpenFilePicker');
    Reflect.deleteProperty(window, 'showSaveFilePicker');
  });
});

/** A document whose member names are long enough to stress the sidebar. */
function wideFixture(): string {
  const c = new MemoryContainer();
  c.writeText('content.md', '# Top {#top}\n\nBody.\n');
  c.writeText(
    'metadata.yaml',
    metadataToYaml({
      title: 'Wide',
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
  c.writeText('assets/Screenshot_2026-05-29_205839_with_a_very_long_name.png', 'x'.repeat(4096));
  c.writeText('assets/another-quite-long-asset-file-name-here.jpg', 'y'.repeat(2048));
  c.writeText('manifest.json', `${manifestToCanonicalJson(buildManifest(c))}\n`);

  const path = join(mkdtempSync(join(tmpdir(), 'mcdf-layout-')), 'wide.mcdf');
  writeFileSync(path, c.toTar());
  return path;
}

async function openWide(page: import('@playwright/test').Page): Promise<void> {
  const [chooser] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.getByRole('button', { name: 'Open…' }).click(),
  ]);
  await chooser.setFiles(wideFixture());
  await expect(page.getByText('Container hash')).toBeVisible();
}

// 1920 matters: at 1440 the preview pane is narrower than a 68ch measure, so a
// centred-column regression is invisible there. The reported symptom appeared on
// a ~1940px window.
for (const viewport of [
  { name: 'wide', width: 1920, height: 1080 },
  { name: 'desktop', width: 1440, height: 900 },
  { name: 'laptop', width: 1024, height: 768 },
  { name: 'narrow', width: 640, height: 900 },
]) {
  test(`no horizontal page overflow at ${viewport.name}`, async ({ page }) => {
    await page.setViewportSize({ width: viewport.width, height: viewport.height });
    await page.goto('./');
    await openWide(page);

    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    );
    expect(overflow).toBeLessThanOrEqual(0);
  });
}

test('manifest content stays inside the sidebar', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('./');
  await openWide(page);

  // Measure the table against its own container, not the viewport: the sidebar
  // clips with overflow-x, so a bounding-box check would report "inside" even
  // while the digest column is cut off — the exact symptom reported ("hash is
  // going out of the panel").
  const overflow = await page.locator('.table-scroll').evaluate((el) => ({
    scroll: el.scrollWidth,
    client: el.clientWidth,
  }));
  expect(overflow.scroll).toBeLessThanOrEqual(overflow.client);

  // And the digest text itself must be fully rendered inside its cell.
  const clipped = await page
    .locator('.hash')
    .evaluateAll((els) =>
      els.filter((el) => el.scrollWidth > el.clientWidth + 1).map((el) => el.textContent ?? ''),
    );
  expect(clipped).toEqual([]);
});

test('the preview fills its pane rather than sitting in a narrow column', async ({ page }) => {
  // Wide viewport on purpose: a 68ch column only becomes visibly narrow once the
  // pane is wider than the measure.
  await page.setViewportSize({ width: 1920, height: 1080 });
  await page.goto('./');

  const pane = await page.locator('.preview').boundingBox();
  const article = await page.locator('article.rendered').boundingBox();
  expect(pane).not.toBeNull();
  expect(article).not.toBeNull();

  // Threshold taken from measurement, not intuition: at 1920 the pane is 751px,
  // filling it gives 0.952 (the difference is the pane's own padding), and the
  // 68ch centred column this replaced gave 0.868. 0.93 sits between them with
  // room for font differences.
  const paneWidth = (pane as { width: number }).width;
  const articleWidth = (article as { width: number }).width;
  expect(articleWidth / paneWidth).toBeGreaterThan(0.93);
});
