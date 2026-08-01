// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The browser's side of importing: picking files, resolving the assets a
// document references, and naming the result. All conversion itself lives in
// `mcdf-ts/import` — the same layering law the rest of this app follows, so the
// CLI and this client convert identically and neither owns the rules.
//
// The asset story is the part that is genuinely browser-shaped. A `.md` or
// `.html` file names images by relative path, and a browser has no folder to
// look in — so the picker accepts a multiple selection and the user brings the
// images along with the document. That needs no new API and no permission
// prompt, which the directory-picking alternative needs both of.
//
// The converters load on demand. turndown brings an HTML-to-Markdown engine
// that most sessions never use, and making first paint wait on it is the same
// mistake code-splitting the editor fixed: detection comes from the core entry
// point and is free, the converters arrive when someone actually imports.

import {
  detectImportFormat,
  IMPORT_ACCEPT,
  type AssetResolver,
  type ImportFormat,
  type ImportOptions,
  type ImportResult,
} from 'mcdf-ts';

import { openFilesViaInput, type OpenedFile } from './files.ts';

export interface PickedFile {
  name: string;
  bytes: Uint8Array;
}

/** Timestamp shape the audit chain uses elsewhere in this app. */
function now(): string {
  return `${new Date().toISOString().slice(0, 19)}Z`;
}

/** `book.epub` -> `book.mcdf`; the result is a new document, not the source. */
export function importedFileName(sourceName: string): string {
  const base = sourceName.split(/[/\\]/).pop() ?? sourceName;
  const dot = base.lastIndexOf('.');
  return `${dot <= 0 ? base : base.slice(0, dot)}.mcdf`;
}

/**
 * Builds an asset resolver over the files the user selected alongside the
 * document.
 *
 * Matched on the full relative path first, then on the basename. The basename
 * fallback is what makes this work at all: a browser file input reports only
 * the leaf name, so `images/fig.png` in the source has to find `fig.png` in the
 * selection. It is a heuristic, and it is why an unresolved image is reported
 * in the conversion report rather than passed over.
 */
export function assetResolverFrom(files: readonly PickedFile[]): AssetResolver {
  const byName = new Map<string, Uint8Array>();
  for (const file of files) {
    const leaf = file.name.split(/[/\\]/).pop() ?? file.name;
    if (!byName.has(file.name)) byName.set(file.name, file.bytes);
    if (!byName.has(leaf)) byName.set(leaf, file.bytes);
  }
  return (relativePath) => {
    const direct = byName.get(relativePath);
    if (direct !== undefined) return direct;
    const leaf = relativePath.split('/').pop() ?? relativePath;
    return byName.get(leaf) ?? null;
  };
}

export interface ImportOutcome extends ImportResult {
  fileName: string;
}

/**
 * Converts one already-read file, using the rest of the selection for assets.
 *
 * Throws when the file is not something an importer handles — deciding by
 * extension, so the conversion report names the format the user thought they
 * were importing.
 */
export async function runImport(
  document: PickedFile,
  companions: readonly PickedFile[] = [],
): Promise<ImportOutcome> {
  const format = detectImportFormat(document.name);
  if (format === null) {
    throw new Error(
      `${document.name} is not a format this client can import. ` +
        'Supported: Markdown (.md), HTML (.html) and EPUB (.epub).',
    );
  }

  const options = {
    now: now(),
    actor: 'mcdf-web',
    source: document.name,
    resolveAsset: assetResolverFrom(companions),
  };

  const result = await convert(format, document, options);
  return { ...result, fileName: importedFileName(document.name) };
}

async function convert(
  format: ImportFormat,
  document: PickedFile,
  options: ImportOptions,
): Promise<ImportResult> {
  const { importEpub, importHtml, importMarkdown } = await import('mcdf-ts/import');
  switch (format) {
    case 'markdown':
      return importMarkdown(new TextDecoder().decode(document.bytes), options);
    case 'html':
      return importHtml(new TextDecoder().decode(document.bytes), options);
    case 'epub':
      // Assets come from inside the archive, so the resolver is unused here.
      return importEpub(document.bytes, options);
  }
}

/** True for a file the client should import rather than open. */
export function isImportable(name: string): boolean {
  return detectImportFormat(name) !== null;
}

export { IMPORT_ACCEPT };

/**
 * Opens the import picker, allowing several files so images can come along.
 *
 * Uses the plain input element rather than the File System Access picker on
 * purpose: an imported document has no file to write back to, so the writable
 * handle the fancy picker exists to provide would be discarded anyway.
 */
export async function pickForImport(): Promise<PickedFile[]> {
  const files = await openFilesViaInput(`${IMPORT_ACCEPT},image/*`, true);
  return files.map(toPicked);
}

function toPicked(file: OpenedFile): PickedFile {
  return { name: file.name, bytes: file.bytes };
}
