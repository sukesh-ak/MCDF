// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// HTML import: a saved web page or an exported document becomes an MCDF
// container. Mirrors `mcdf import-html` on the desktop.
//
// Input is real-world HTML — tag soup, not XHTML. The desktop needed a tolerant
// normalizer of its own to get there (`core/html_normalize.hpp`) because pugixml
// wants well-formed XML; here the platform's HTML parser is already the most
// tolerant one available and is what turndown runs on, so that whole layer has
// no counterpart and needs none.

import { emptyMetadata } from '../model/types.js';
import { compose } from './compose.js';
import { demoteHeadings, extractTitle, takeLeadingHeading, trimBlankLines } from './shape.js';
import { createConverter, newConversionState, type ConversionState } from './turndown-setup.js';
import type { ImportOptions, ImportResult } from './types.js';

export interface HtmlImportOptions extends ImportOptions {
  /** Directory the page sat in, so its relative image paths resolve. */
  baseDir?: string;
}

/**
 * Converts one HTML document to Markdown and reports the title it found.
 *
 * Shared with the EPUB importer, which runs it once per spine document.
 */
export function convertDocument(
  html: string,
  baseDir: string,
  state: ConversionState,
): { title: string; markdown: string } {
  const converter = createConverter(baseDir, state);
  let markdown = trimBlankLines(converter.turndown(html));

  let title = extractTitle(html);
  const leading = takeLeadingHeading(markdown);
  if (title === '') {
    // No <title>: the document's own first heading names it, and is then not
    // repeated in the body.
    title = leading.title;
    if (leading.title !== '') markdown = leading.rest;
  } else if (leading.title === title) {
    markdown = leading.rest;
  }

  return { title, markdown: demoteHeadings(trimBlankLines(markdown)) };
}

/** Notes every HTML-derived import owes the user, in a stable order. */
export function conversionNotes(state: ConversionState): string[] {
  const notes = ['styling (CSS), fonts and page layout are not carried over'];
  if (state.droppedTables) {
    notes.push('tables with merged or ragged cells were flattened to plain text');
  }
  if (state.droppedMath) notes.push('MathML content was dropped');
  if (state.droppedIframes) {
    notes.push('embedded media and frames (video, audio, iframe) were dropped');
  }
  if (state.escaped.length > 0) {
    notes.push(
      `${state.escaped.length} reference${state.escaped.length === 1 ? '' : 's'} pointed ` +
        'outside the source and were not followed',
    );
  }
  return notes;
}

export async function importHtml(
  html: string,
  options: HtmlImportOptions,
): Promise<ImportResult> {
  const state = newConversionState();
  const baseDir = options.baseDir ?? '';
  const { title, markdown } = convertDocument(html, baseDir, state);

  const body = trimBlankLines(markdown);
  if (body === '' && title === '') {
    throw new Error('this HTML produced no readable content');
  }

  const heading = title === '' ? 'Untitled' : title;
  const content = `# ${heading} {#document}\n\n${body}\n`;

  const assets = new Map<string, Uint8Array>();
  let missing = 0;
  for (const [source, target] of state.images) {
    const data = await options.resolveAsset?.(source);
    if (data == null) {
      missing++;
      continue;
    }
    assets.set(target, data);
  }

  const notes = conversionNotes(state);
  if (missing > 0) {
    notes.push(
      `${missing} image${missing === 1 ? '' : 's'} could not be read from the source and ` +
        `${missing === 1 ? 'is' : 'are'} referenced but not included`,
    );
  }

  const metadata = emptyMetadata();
  metadata.title = title;

  const { container, title: finalTitle } = compose(
    {
      format: 'html',
      markdown: content,
      metadata,
      // One section, because the document genuinely has one: the H1 above
      // carries `{#document}` and this binds to it, so an HTML import arrives
      // at the Core profile already satisfied rather than reporting an unbound
      // schema the user has to go and fix.
      sections: [{ id: 'document', title: heading, required: true }],
      documentType: 'document',
      assets,
    },
    options,
  );

  return {
    container,
    report: {
      format: 'html',
      source: options.source ?? '',
      title: finalTitle,
      authors: [],
      chapters: 1,
      images: assets.size,
      notes,
    },
  };
}
