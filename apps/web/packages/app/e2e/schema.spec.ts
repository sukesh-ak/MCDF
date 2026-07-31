// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Reproduces the real-world mismatch: a container whose schema.yaml declares
// sections that its content.md never anchors (a schema carried over from a
// different document). Reporting E_SCHEMA_UNBOUND is correct — the reference
// C++ CLI reports it identically — so what these tests cover is the *repair*.

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

/** Content with real headings, schema sections that match none of them. */
function mismatchedFixture(): string {
  const c = new MemoryContainer();
  c.writeText('content.md', '# Project Readme\n\nIntro.\n\n## Status\n\nEarly.\n\n## Build\n\nCMake.\n');
  c.writeText(
    'metadata.yaml',
    metadataToYaml({
      title: 'Mismatched',
      version: '1.0.0',
      authors: [],
      created_at: '',
      classification: '',
      language: 'en',
    }),
  );
  c.writeText(
    'schema.yaml',
    schemaToYaml({
      document_type: 'contract',
      sections: [
        { id: 'overview', title: 'Contract Overview', required: false },
        { id: 'terms', title: 'Terms and Conditions', required: true },
      ],
    }),
  );
  c.writeText('manifest.json', `${manifestToCanonicalJson(buildManifest(c))}\n`);

  const path = join(mkdtempSync(join(tmpdir(), 'mcdf-schema-')), 'mismatched.mcdf');
  writeFileSync(path, c.toTar());
  return path;
}

/** The Conformance panel's issue list — the codes also appear in Structure. */
function issues(page: import('@playwright/test').Page) {
  return page.locator('.issues');
}

async function openMismatched(page: import('@playwright/test').Page): Promise<void> {
  await page.goto('./');
  const [chooser] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.getByRole('button', { name: 'Open…' }).click(),
  ]);
  await chooser.setFiles(mismatchedFixture());
  await expect(issues(page).getByText('E_SCHEMA_UNBOUND')).toBeVisible();
  await expect(issues(page).getByText('E_REQUIRED_SECTION_MISSING')).toBeVisible();
}

/**
 * Heading line numbers in the fixture above — the option values.
 *
 *   1  # Project Readme      5  ## Status      9  ## Build
 */
const HEADING_LINE = { readme: '1', status: '5', build: '9' };

/** Selects a heading for an unbound section and applies the binding. */
async function bindSection(
  page: import('@playwright/test').Page,
  section: string,
  headingLine: string,
): Promise<void> {
  const select = page.getByLabel(new RegExp(`Bind\\s+#${section} to`));
  await select.selectOption(headingLine);
  await page
    .locator('li', { has: select })
    .getByRole('button', { name: 'Bind', exact: true })
    .click();
}

test('binding each section to a heading clears both errors', async ({ page }) => {
  await openMismatched(page);

  await bindSection(page, 'overview', HEADING_LINE.readme);
  await bindSection(page, 'terms', HEADING_LINE.status);

  // The structural errors are gone.
  await expect(issues(page).getByText('E_SCHEMA_UNBOUND')).toHaveCount(0);
  await expect(issues(page).getByText('E_REQUIRED_SECTION_MISSING')).toHaveCount(0);
  await page.getByLabel('Profile').selectOption('core');
  await expect(page.getByText('Valid at the core profile.')).toBeVisible();

  // Integrity is a different matter: the repair edited content.md, so the
  // manifest is legitimately stale until the document is saved. Asserting this
  // rather than working around it — it is the behaviour the format intends.
  await page.getByLabel('Profile').selectOption('integrity');
  await expect(issues(page).getByText('E_MANIFEST_HASH_MISMATCH')).toBeVisible();

  // And the fix is in the source, where a reader would expect it.
  const editor = page.getByRole('textbox', { name: 'Document source, CommonMark' });
  await expect(editor).toContainText('# Project Readme {#overview}');
  await expect(editor).toContainText('## Status {#terms}');
});

test('rebuilding the schema from content is the other way out', async ({ page }) => {
  await openMismatched(page);

  // Anchor one heading by hand, then let the schema adopt what content declares.
  await bindSection(page, 'overview', HEADING_LINE.readme);
  await page.getByRole('button', { name: 'Rebuild from content' }).click();

  await expect(issues(page).getByText('E_SCHEMA_UNBOUND')).toHaveCount(0);
  await expect(issues(page).getByText('E_REQUIRED_SECTION_MISSING')).toHaveCount(0);
  await page.getByLabel('Profile').selectOption('core');
  await expect(page.getByText('Valid at the core profile.')).toBeVisible();
});

test('the schema panel reports which sections bind', async ({ page }) => {
  await openMismatched(page);

  const schema = page.locator('details', { has: page.getByText('Schema', { exact: true }) });
  await expect(schema.getByText('No heading carries')).toHaveCount(2);

  await bindSection(page, 'terms', HEADING_LINE.build);

  await expect(schema.getByText('Bound to a heading.')).toHaveCount(1);
  await expect(schema.getByText('No heading carries')).toHaveCount(1);
});

test('the outline moves the editor caret to the heading', async ({ page }) => {
  await openMismatched(page);

  // The outline entry for the "Build" heading, not the schema panel's buttons.
  await page.locator('nav[aria-label="Document outline"] button', { hasText: 'Build' }).click();

  const editor = page.getByRole('textbox', { name: 'Document source, CommonMark' });
  await expect(editor).toBeFocused();
  await expect(page.locator('[aria-live="polite"].visually-hidden')).toContainText(/Moved to/);
});
