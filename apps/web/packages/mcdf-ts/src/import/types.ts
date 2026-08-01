// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import type { MemoryContainer } from '../container/container.js';

export type ImportFormat = 'markdown' | 'html' | 'epub';

/**
 * What an import produced, and what it could not carry over.
 *
 * Imports are best-effort conversions, so the caller MUST be able to tell the
 * user what was dropped — the conversion report is a requirement of the import
 * design, not a nicety, and the same text lands in the audit entry.
 */
export interface ImportReport {
  format: ImportFormat;
  /** Name of the source file, when the caller knew it. */
  source: string;
  title: string;
  authors: string[];
  /** Top-level sections produced. 1 for a single-document import. */
  chapters: number;
  images: number;
  /** Human-readable "dropped or changed" notes, in the order they were found. */
  notes: string[];
}

export interface ImportResult {
  container: MemoryContainer;
  report: ImportReport;
}

/**
 * Supplies the bytes of an asset the source document references, given its path
 * relative to that document. Return null when it cannot be found — a broken
 * image reference is reported, never fatal.
 *
 * A callback rather than a directory, so this package needs no ambient
 * filesystem access and works unchanged in a browser, in a worker and in Node.
 * The reference implementation draws the same line with `mcdf::AssetResolver`.
 */
export type AssetResolver = (
  relativePath: string,
) => Uint8Array | null | Promise<Uint8Array | null>;

export interface ImportOptions {
  /**
   * RFC 3339 timestamp for the `IMPORTED` audit entry.
   *
   * A parameter rather than the clock, for the reason `auditAppend` already
   * gives: the core has no business deciding what time it is, and tests need
   * reproducible entry hashes.
   */
  now: string;
  /** Audit actor. The browser client passes `mcdf-web`. */
  actor?: string;
  /** Name of the file being imported, recorded in the report. */
  source?: string;
  /** Resolves images and other referenced files. */
  resolveAsset?: AssetResolver;
}

export const DEFAULT_ACTOR = 'mcdf-ts';

// -- format detection --------------------------------------------------------
//
// These live here, and this module is re-exported from the package's main
// entry point, so a caller can ask "is this importable?" and build a file
// picker without loading a converter. The converters pull in an HTML parser;
// deciding whether to offer the button should not.

/** File extensions each importer accepts, for pickers and drop targets. */
export const IMPORT_EXTENSIONS: Record<ImportFormat, string[]> = {
  markdown: ['.md', '.markdown', '.mdown', '.mkd'],
  html: ['.html', '.htm', '.xhtml'],
  epub: ['.epub'],
};

/** The `accept` string for a file input offering every supported import. */
export const IMPORT_ACCEPT = Object.values(IMPORT_EXTENSIONS).flat().join(',');

/**
 * Picks an importer from a file name.
 *
 * By extension only, deliberately. Sniffing content would let a file claiming
 * to be one thing be imported as another, and the conversion report names the
 * format it used — that claim should match what the user thought they dropped.
 * Returns null for anything unrecognised, including `.mcdf`, which is opened
 * rather than imported.
 */
export function detectImportFormat(fileName: string): ImportFormat | null {
  const lower = fileName.toLowerCase();
  for (const [format, extensions] of Object.entries(IMPORT_EXTENSIONS)) {
    if (extensions.some((ext) => lower.endsWith(ext))) return format as ImportFormat;
  }
  return null;
}
