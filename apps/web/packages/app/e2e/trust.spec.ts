// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The trust and encryption flows in a real browser: generate a key, sign, break the signature
// by typing, re-sign. This is the demo the format exists to make obvious, so it
// is tested end to end rather than only at the unit level — and the assertions
// are on what a screen-reader user is told, not only on what is coloured.

import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';

const WCAG = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];

/** Panels are `<details>`; their summaries are not buttons, so target them directly. */
function panel(page: Page, title: string) {
  return page.locator('summary').filter({ hasText: title });
}

/** Opens every collapsed panel, so axe sees the whole UI rather than summaries. */
async function expandAllPanels(page: Page): Promise<void> {
  const collapsed = page.locator('details:not([open]) > summary');
  // Each click removes one from the set, so re-query rather than index. The
  // bound stops a panel that refuses to open from hanging the run.
  for (let guard = 0; guard < 20 && (await collapsed.count()) > 0; guard++) {
    await collapsed.first().click();
  }
}

async function generateKey(page: Page, label: string): Promise<void> {
  await page.getByLabel('Label').fill(label);
  await page.getByRole('button', { name: 'Generate', exact: true }).click();
  // The keyring list, not the <select> options — an option is not "visible".
  await expect(page.locator('ul.keys')).toContainText(label);
}

test.describe('sign, break, re-sign', () => {
  test('a signature goes invalid while typing and is repaired by re-signing', async ({ page }) => {
    await page.goto('./');
    await page.getByRole('button', { name: /Add manifest/ }).click();

    await generateKey(page, 'Demo key');
    await page.getByRole('button', { name: /^Sign the manifest$/ }).click();

    // Signed and valid. Scoped to the signature list: the panel also names the
    // destination path in its help text.
    const signatures = page.locator('ul.signatures');
    await expect(signatures).toContainText('signatures/author.sig');
    await expect(page.locator('li.valid').first()).toBeVisible();

    // Typing changes the manifest, so the signature no longer covers it.
    const editor = page.getByRole('textbox', { name: 'Document source, CommonMark' });
    await editor.click();
    await page.keyboard.type('tampered');
    await expect(editor).toContainText('tampered');

    await expect(page.getByRole('alert')).toContainText('Signature no longer verifies');
    await expect(page.locator('li.invalid').first()).toBeVisible();

    // Trust looks ahead — it asks whether the signature would still hold once
    // this is saved. Validation describes the container as it stands, where the
    // manifest has not been rebuilt yet, so it reports that instead.
    await page.getByLabel('Profile').selectOption('signed');
    await expect(page.locator('.issues')).toContainText('E_MANIFEST_HASH_MISMATCH');

    // Rebuild the manifest and the two agree: integrity is restored, trust is
    // not, and the normative code says so.
    await page.getByRole('button', { name: /Recompute now/ }).click();
    await expect(page.locator('.issues')).toContainText('E_SIG_INVALID');
    await expect(page.locator('.issues')).not.toContainText('E_MANIFEST_HASH_MISMATCH');

    // Nothing is lost: re-signing restores trust.
    await page.getByRole('button', { name: /^Re-sign the manifest$/ }).click();
    await expect(page.locator('li.valid').first()).toBeVisible();
    await expect(page.locator('li.invalid')).toHaveCount(0);
  });
});

test.describe('confidentiality', () => {
  test('encrypting seals the editor and decrypting brings it back', async ({ page }) => {
    await page.goto('./');
    await page.getByRole('button', { name: /Add manifest/ }).click();

    // An X25519 key is a recipient, not a signer.
    await page.getByLabel('New key type').selectOption('X25519');
    await generateKey(page, 'My recipient key');

    await panel(page, 'Encryption').click();
    await page.getByRole('checkbox', { name: /My recipient key/ }).check();
    await page.getByRole('button', { name: /^Encrypt 1 member$/ }).click();

    // The editor is replaced by an explanation, not an empty box.
    await expect(page.getByText("This document's content is encrypted.")).toBeVisible();
    await expect(page.getByRole('textbox', { name: 'Document source, CommonMark' })).toHaveCount(0);
    await expect(page.locator('p.state')).toContainText('aes-256-gcm');

    await page.getByRole('button', { name: 'Decrypt', exact: true }).click();
    await expect(page.getByRole('textbox', { name: 'Document source, CommonMark' })).toBeVisible();
  });
});

test.describe('audit', () => {
  test('appends a chained entry and checkpoints it', async ({ page }) => {
    await page.goto('./');
    await generateKey(page, 'Audit key');

    await panel(page, 'Audit').click();
    await page.getByLabel('Action').fill('REVIEWED');
    await page.getByLabel('Actor').fill('Sam');
    await page.getByRole('button', { name: 'Append', exact: true }).click();

    await expect(page.locator('ol.timeline')).toContainText('REVIEWED');
    await expect(page.getByText('Chain intact from genesis (1 entries).')).toBeVisible();

    await page.getByRole('button', { name: 'Write checkpoint' }).click();
    await expect(page.getByText(/Checkpoint valid/)).toBeVisible();
  });
});

test.describe('accessibility of the new panels', () => {
  test('every panel expanded has no axe violations, in both colour schemes', async ({ page }) => {
    for (const scheme of ['light', 'dark'] as const) {
      await page.emulateMedia({ colorScheme: scheme });
      await page.goto('./');
      await expandAllPanels(page);
      const results = await new AxeBuilder({ page }).withTags(WCAG).analyze();
      expect(results.violations, `${scheme} scheme`).toEqual([]);
    }
  });

  test('the diff view is reachable and labelled', async ({ page }) => {
    await page.goto('./');
    const editor = page.getByRole('textbox', { name: 'Document source, CommonMark' });
    await editor.click();
    await page.keyboard.type('a change');

    await panel(page, 'Diff').click();
    await expect(page.locator('ul.changes')).toContainText('content.md');

    await page.getByRole('button', { name: /side by side/ }).click();
    await expect(
      page.getByRole('region', { name: /baseline on the left and current on the right/ }),
    ).toBeVisible();

    const results = await new AxeBuilder({ page }).withTags(WCAG).analyze();
    expect(results.violations).toEqual([]);
  });
});
