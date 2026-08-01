// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The browser's import glue. The conversion itself is tested in mcdf-ts; what
// is tested here is everything the browser adds — naming the result, finding
// the document among a multi-file selection, and resolving images from files
// the user brought along instead of from a folder nobody can see.

import { loadDocument, validate } from 'mcdf-ts';
import { describe, expect, it } from 'vitest';

import { assetResolverFrom, importedFileName, isImportable, runImport } from './import.ts';

const utf8 = (text: string) => new TextEncoder().encode(text);
const png = new Uint8Array([0x89, 0x50, 0x4e, 0x47]);

describe('importedFileName', () => {
  it('renames the source to a .mcdf document', () => {
    expect(importedFileName('Moby-Dick.epub')).toBe('Moby-Dick.mcdf');
    expect(importedFileName('notes.md')).toBe('notes.mcdf');
    expect(importedFileName('page.HTML')).toBe('page.mcdf');
  });

  it('handles a name with no extension and a path prefix', () => {
    expect(importedFileName('README')).toBe('README.mcdf');
    expect(importedFileName('docs/guide.md')).toBe('guide.mcdf');
  });

  it('leaves a leading dot alone rather than eating the whole name', () => {
    expect(importedFileName('.hidden')).toBe('.hidden.mcdf');
  });
});

describe('isImportable', () => {
  it('accepts the formats this client converts', () => {
    expect(isImportable('a.md')).toBe(true);
    expect(isImportable('a.markdown')).toBe(true);
    expect(isImportable('A.HTM')).toBe(true);
    expect(isImportable('book.epub')).toBe(true);
  });

  it('rejects everything else, including .mcdf', () => {
    // A container is opened, never imported — routing it here would convert a
    // document into a copy of itself.
    expect(isImportable('doc.mcdf')).toBe(false);
    expect(isImportable('sheet.xlsx')).toBe(false);
    expect(isImportable('noextension')).toBe(false);
  });
});

describe('assetResolverFrom', () => {
  it('matches on the full path when the caller has one', () => {
    const resolve = assetResolverFrom([{ name: 'images/fig.png', bytes: png }]);
    expect(resolve('images/fig.png')).toEqual(png);
  });

  it('falls back to the basename, which is all a file input reports', () => {
    // The document says `images/fig.png`; the browser hands over `fig.png`.
    // Without this fallback no image a user selected would ever be found.
    const resolve = assetResolverFrom([{ name: 'fig.png', bytes: png }]);
    expect(resolve('images/fig.png')).toEqual(png);
  });

  it('returns null for something that was not selected', () => {
    expect(assetResolverFrom([])('images/fig.png')).toBeNull();
  });

  it('keeps the first of two files with the same basename', () => {
    const other = new Uint8Array([1, 2, 3]);
    const resolve = assetResolverFrom([
      { name: 'a/fig.png', bytes: png },
      { name: 'b/fig.png', bytes: other },
    ]);
    expect(resolve('fig.png')).toEqual(png);
  });
});

describe('runImport', () => {
  it('converts Markdown and names the result', async () => {
    const outcome = await runImport({ name: 'notes.md', bytes: utf8('# Notes {#n}\n\nBody.\n') });

    expect(outcome.fileName).toBe('notes.mcdf');
    expect(outcome.report.format).toBe('markdown');
    expect(outcome.report.source).toBe('notes.md');
    expect(validate(outcome.container, loadDocument(outcome.container), 'integrity').ok).toBe(
      true,
    );
  });

  it('resolves an image from the files selected alongside the document', async () => {
    const outcome = await runImport(
      { name: 'notes.md', bytes: utf8('# N\n\n![fig](images/fig.png)\n') },
      [{ name: 'fig.png', bytes: png }],
    );

    expect(outcome.report.images).toBe(1);
    expect(outcome.container.read('assets/fig.png')).toEqual(png);
  });

  it('reports an image nobody selected instead of failing the import', async () => {
    const outcome = await runImport({
      name: 'notes.md',
      bytes: utf8('# N\n\n![fig](images/fig.png)\n'),
    });

    expect(outcome.report.images).toBe(0);
    expect(outcome.report.notes.join(' ')).toMatch(/could not be found/);
  });

  it('converts HTML', async () => {
    const outcome = await runImport({
      name: 'page.html',
      bytes: utf8('<html><head><title>Page</title></head><body><p>Hello.</p></body></html>'),
    });

    expect(outcome.report.format).toBe('html');
    expect(outcome.container.readText('content.md')).toContain('# Page {#document}');
  });

  it('records a timestamp the audit chain accepts', async () => {
    const outcome = await runImport({ name: 'a.md', bytes: utf8('# A\n') });
    const log = outcome.container.readText('audit.log');
    expect(log).toContain('IMPORTED');
    expect(log).toContain('mcdf-web');
    // The same RFC 3339 shape the rest of the app appends.
    expect(log).toMatch(/\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z/);
  });

  it('refuses a format it does not handle', async () => {
    await expect(runImport({ name: 'sheet.xlsx', bytes: png })).rejects.toThrow(
      /not a format this client can import/,
    );
  });
});
