// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Importers: getting documents people already have *into* MCDF.
//
// A separate entry point (`mcdf-ts/import`) rather than part of the main index,
// so the core keeps its headline property — no DOM, and dependency-light enough
// to run in a worker — while the converters are free to use the platform's HTML
// parser through turndown. Importing nothing from here costs nothing.
//
// Format *detection* deliberately lives in the main entry point instead
// (`detectImportFormat`, `IMPORT_ACCEPT`): a client has to decide whether a
// file is importable, and build a picker, long before it converts anything, and
// that decision should not drag in a parser. This module re-exports it so a
// consumer of the converters still has it to hand.

export { importMarkdown, type MarkdownImportOptions } from './markdown.js';
export { importHtml, type HtmlImportOptions } from './html.js';
export { importEpub, EpubImportError } from './epub.js';
export { generatedBy } from './compose.js';
export { ZipArchive, ZipError, type ZipEntry, type ZipReadLimits } from './zip.js';
export { parseXmlElements, XmlError, type XmlElement } from './xml.js';
export * from './types.js';
