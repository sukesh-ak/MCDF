// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Importing, end to end in a real browser.
//
// The point of covering this here rather than only in unit tests is that the
// browser is where importing actually matters: it is the one client with no CLI
// to fall back on, and the conversion report is a modal dialog, which is the
// single most reliable way to ship an accessibility regression.

import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Page } from '@playwright/test';

const WCAG = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];

const PAGE_HTML = `<!doctype html>
<html><head><title>Imported Page</title></head>
<body><h1>Imported Page</h1><p>Converted <em>body</em> text.</p><h2>Details</h2>
<ul><li>First</li><li>Second</li></ul></body></html>`;

const MARKDOWN = `# Imported Notes {#notes}

Some **body** text.
`;

/** Panels are `<details>`; their summaries are not buttons, so target them directly. */
function panel(page: Page, title: string) {
  return page.locator('summary').filter({ hasText: title });
}

/** Puts a file into the import picker without touching the OS file dialog. */
async function importFile(page: Page, name: string, contents: string): Promise<void> {
  const chooser = page.waitForEvent('filechooser');
  await page.getByRole('button', { name: 'Import…' }).click();
  await (await chooser).setFiles({
    name,
    mimeType: 'application/octet-stream',
    buffer: Buffer.from(contents, 'utf8'),
  });
}

test.describe('import', () => {
  test('converts an HTML page and reports what came across', async ({ page }) => {
    await page.goto('./');
    await importFile(page, 'page.html', PAGE_HTML);

    const dialog = page.getByRole('dialog');
    await expect(dialog).toBeVisible();
    await expect(dialog.getByRole('heading', { name: 'Imported from HTML' })).toBeVisible();
    await expect(dialog).toContainText('page.html');
    await expect(dialog).toContainText('page.mcdf');
    // The honest part: conversion always states what it could not carry.
    await expect(dialog).toContainText('styling (CSS), fonts and page layout');

    await dialog.getByRole('button', { name: 'Open the document' }).click();
    await expect(dialog).toBeHidden();

    await expect(page.getByRole('main')).toContainText('Imported Page');
    await expect(page.locator('.filename')).toContainText('page.mcdf');
    // Never silently clean: the converted document exists only in this tab.
    await expect(page.locator('.filename')).toContainText('unsaved changes');
  });

  test('the conversion report has no axe violations', async ({ page }) => {
    await page.goto('./');
    await importFile(page, 'notes.md', MARKDOWN);
    await expect(page.getByRole('dialog')).toBeVisible();

    const results = await new AxeBuilder({ page }).withTags(WCAG).analyze();
    expect(results.violations).toEqual([]);
  });

  test('the dialog takes focus, traps Tab and gives focus back', async ({ page }) => {
    await page.goto('./');
    await importFile(page, 'notes.md', MARKDOWN);

    const accept = page.getByRole('button', { name: 'Open the document' });
    const discard = page.getByRole('button', { name: 'Discard' });
    await expect(accept).toBeFocused();

    // Forward off the last control wraps to the first, so Tab never reaches the
    // toolbar behind the overlay — inert to the eye, reachable to the keyboard.
    await page.keyboard.press('Tab');
    await expect(discard).toBeFocused();
    await page.keyboard.press('Tab');
    await expect(accept).toBeFocused();
    await page.keyboard.press('Shift+Tab');
    await expect(discard).toBeFocused();

    await page.keyboard.press('Escape');
    await expect(page.getByRole('dialog')).toBeHidden();
    // Focus returns to the control that opened it, rather than being stranded
    // on <body> at the top of the page.
    await expect(page.getByRole('button', { name: 'Import…' })).toBeFocused();
  });

  test('discarding leaves the open document untouched', async ({ page }) => {
    await page.goto('./');
    await expect(page.locator('.filename')).toContainText('untitled.mcdf');

    await importFile(page, 'notes.md', MARKDOWN);
    await page.getByRole('dialog').getByRole('button', { name: 'Discard' }).click();

    await expect(page.getByRole('dialog')).toBeHidden();
    await expect(page.locator('.filename')).toContainText('untitled.mcdf');
    await expect(page.getByRole('main')).not.toContainText('Imported Notes');
  });

  test('an imported document is valid and carries its provenance', async ({ page }) => {
    await page.goto('./');
    await importFile(page, 'page.html', PAGE_HTML);
    await page.getByRole('dialog').getByRole('button', { name: 'Open the document' }).click();

    // Conformance panel: the import has to produce a document that passes, not
    // merely one that opens.
    await expect(page.getByText(/Valid at the integrity profile/)).toBeVisible();

    await panel(page, 'Audit').click();
    await expect(page.getByRole('complementary')).toContainText('IMPORTED');
  });

  test('refuses a file it cannot convert, without breaking the page', async ({ page }) => {
    await page.goto('./');
    await importFile(page, 'sheet.xlsx', 'not really a spreadsheet');

    // Two alerts, and both are wanted: the visible banner and the assertive
    // live region. A refusal a screen-reader user never hears is a refusal that
    // looks to them like nothing happened.
    await expect(page.locator('p[role="alert"]')).toContainText(/can import/);
    await expect(page.locator('div[role="alert"]')).toContainText(/can import/);
    await expect(page.getByRole('dialog')).toBeHidden();
    await expect(page.locator('.filename')).toContainText('untitled.mcdf');
  });
});
