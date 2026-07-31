// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Automated accessibility checks. axe catches roughly a third of
// WCAG failures, so these run alongside — never instead of — the manual
// NVDA/VoiceOver pass each milestone requires.

import AxeBuilder from '@axe-core/playwright';
import { expect, test } from '@playwright/test';

const WCAG = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];

async function scan(page: import('@playwright/test').Page) {
  return new AxeBuilder({ page }).withTags(WCAG).analyze();
}

test.describe('accessibility', () => {
  test('the default view has no axe violations', async ({ page }) => {
    await page.goto('./');
    await expect(page.getByRole('heading', { name: 'MCDF Web', level: 1 })).toBeVisible();
    expect((await scan(page)).violations).toEqual([]);
  });

  test('editor-only and preview-only layouts have no axe violations', async ({ page }) => {
    await page.goto('./');
    for (const layout of ['Editor', 'Preview']) {
      await page.getByRole('radio', { name: layout, exact: true }).check();
      expect((await scan(page)).violations).toEqual([]);
    }
  });

  test('both colour schemes have no axe violations', async ({ page }) => {
    for (const scheme of ['light', 'dark'] as const) {
      await page.emulateMedia({ colorScheme: scheme });
      await page.goto('./');
      expect((await scan(page)).violations).toEqual([]);
    }
  });

  test('landmarks and the skip link are present', async ({ page }) => {
    await page.goto('./');
    await expect(page.getByRole('banner')).toBeVisible();
    await expect(page.getByRole('main')).toBeVisible();
    await expect(page.getByRole('complementary', { name: 'Document panels' })).toBeVisible();
    await expect(page.getByRole('contentinfo')).toBeVisible();

    // The skip link is the first thing keyboard users reach.
    await page.keyboard.press('Tab');
    await expect(page.getByRole('link', { name: 'Skip to the editor' })).toBeFocused();
  });

  test('every action is reachable by keyboard alone', async ({ page }) => {
    await page.goto('./');
    const reached = new Set<string>();
    for (let i = 0; i < 40; i++) {
      await page.keyboard.press('Tab');
      const label = await page.evaluate(() => {
        const el = document.activeElement;
        if (el === null) return '';
        return (el.getAttribute('aria-label') ?? el.textContent ?? '').trim().slice(0, 40);
      });
      if (label !== '') reached.add(label);
    }
    for (const action of ['New', 'Open…', 'Save', 'Save as…']) {
      expect([...reached].some((l) => l.startsWith(action))).toBe(true);
    }
  });
});

test.describe('the tamper demo is announced, not just shown', () => {
  test('editing a document breaks its manifest audibly', async ({ page }) => {
    await page.goto('./');

    // Give the starting document a manifest, then change its content.
    await page.getByRole('button', { name: /Add manifest/ }).click();
    await expect(page.getByText('up to date')).toBeVisible();

    const editor = page.getByRole('textbox', { name: 'Document source, CommonMark' });
    await editor.click();
    await page.keyboard.type('tampered');

    // Assert the edit actually landed. Without this the test passes on a single
    // surviving keystroke, which is exactly how a focus-losing editor slipped
    // through once already.
    await expect(editor).toContainText('tampered');
    await expect(editor).toBeFocused();

    // The status is conveyed to assistive technology, not only by colour.
    const alert = page.getByRole('alert');
    await expect(alert).toContainText('Manifest no longer matches the document');
    await expect(page.getByText('E_MANIFEST_HASH_MISMATCH')).toBeVisible();
  });
});
