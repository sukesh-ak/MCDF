// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The HTML/XHTML -> Markdown converter, shared by the HTML and EPUB importers
// exactly as the reference implementation shares `core/xhtml_md.hpp` between
// `import_html` and `import_epub`.
//
// turndown does the walking. What is configured here is everything MCDF cares
// about that a general-purpose converter has no opinion on: ATX headings
// (`{#id}` anchors attach to `#`, never to a setext underline), images pulled
// into `assets/` as container members, and tables kept as GFM pipe tables when
// they are regular enough to survive it.
//
// Why turndown rather than in-house: HTML-to-Markdown is a large pile of
// well-trodden edge cases with no MCDF-specific content, which is the opposite
// of the ZIP and XML readers beside it — those are small, bounded, and needed
// to be dependency-free to run where they run.

import TurndownService from 'turndown';

import { attr, first, toArray, type DomElement, type DomNode } from './dom.js';
import { assetPath, decodeHref, isExternal, resolvePath, stripFragment } from './paths.js';

/** An image the converter met: where it came from, and where it now lives. */
export interface ImageRef {
  /** Path within the source archive or relative to the source document. */
  source: string;
  /** Container member path, always under `assets/`. */
  target: string;
}

export interface ConversionState {
  /** Source path -> container path, in the order the images were met. */
  images: Map<string, string>;
  /** Container paths already allocated, so two sources cannot collide. */
  taken: Set<string>;
  droppedTables: boolean;
  droppedMath: boolean;
  droppedIframes: boolean;
  /** Source references that pointed outside the archive and were not followed. */
  escaped: string[];
}

export function newConversionState(): ConversionState {
  return {
    images: new Map(),
    taken: new Set(),
    droppedTables: false,
    droppedMath: false,
    droppedIframes: false,
    escaped: [],
  };
}

function text(node: DomNode): string {
  return (node.textContent ?? '').replace(/\s+/g, ' ').trim();
}

/**
 * Builds a converter for one source document.
 *
 * `baseDir` is the directory the document sits in, so relative hrefs resolve
 * the way they would have for the original reader. `state` accumulates across
 * every document in an EPUB, which is what stops chapter 2's `images/fig.png`
 * from overwriting chapter 1's.
 */
export function createConverter(baseDir: string, state: ConversionState): TurndownService {
  const service = new TurndownService({
    headingStyle: 'atx',
    hr: '---',
    bulletListMarker: '-',
    codeBlockStyle: 'fenced',
    fence: '```',
    emDelimiter: '*',
    strongDelimiter: '**',
    linkStyle: 'inlined',
    br: '  ',
  });

  // Renderer-safety parity with the canonical render path, which treats raw
  // HTML as text rather than markup (spec §10.4): anything that could execute
  // or restyle is removed here rather than carried into content.md as literal
  // HTML that a renderer would then have to neutralise.
  service.remove(['script', 'style', 'head', 'meta', 'link', 'noscript', 'title']);

  service.addRule('mcdf-strikethrough', {
    filter: ['del', 's'],
    replacement: (content) => (content.trim() === '' ? '' : `~~${content}~~`),
  });

  // MathML has no Markdown equivalent and no honest fallback; its text content
  // is a sequence of loose symbols, not an equation. Dropped and reported.
  service.addRule('mcdf-math', {
    filter: (node) => node.nodeName.toLowerCase() === 'math',
    replacement: () => {
      state.droppedMath = true;
      return '';
    },
  });

  service.addRule('mcdf-embed', {
    filter: ['iframe', 'object', 'embed', 'video', 'audio', 'canvas'],
    replacement: () => {
      state.droppedIframes = true;
      return '';
    },
  });

  service.addRule('mcdf-image', {
    filter: 'img',
    replacement: (_content, node) => {
      const element = node as unknown as DomElement;
      const alt = attr(element, 'alt').replace(/[\r\n]+/g, ' ').trim();
      const raw = attr(element, 'src');
      if (raw === '') return '';

      // An external image is left as a link rather than fetched. A document
      // that silently reaches out to the network when opened is precisely what
      // an MCDF container is supposed to stop being (spec §4.1).
      if (isExternal(raw)) return `![${escapeAlt(alt)}](${raw})`;

      const source = resolvePath(baseDir, decodeHref(stripFragment(raw)));
      if (source === '') {
        state.escaped.push(raw);
        return alt === '' ? '' : escapeAlt(alt);
      }

      let target = state.images.get(source);
      if (target === undefined) {
        target = assetPath(source, state.taken);
        state.images.set(source, target);
      }

      // Layout hints ride in the CommonMark title slot, literal `width=` with
      // no spaces around `=` (spec §4.1). Carried over only when the source
      // states a plain pixel width — a percentage or a CSS expression is not
      // something the hint syntax can express, so it is dropped rather than
      // mistranslated.
      const width = attr(element, 'width');
      const hint = /^\d+$/.test(width) ? ` "width=${width}"` : '';
      return `![${escapeAlt(alt)}](${target}${hint})`;
    },
  });

  service.addRule('mcdf-table', {
    filter: 'table',
    replacement: (content, node) => {
      const table = node as unknown as DomElement;
      const pipe = toPipeTable(table);
      if (pipe !== null) return `\n\n${pipe}\n\n`;
      // Irregular: merged cells, nested tables, or no consistent column count.
      // A pipe table cannot represent it, so its text is kept and the loss is
      // reported rather than passed off as a conversion.
      state.droppedTables = true;
      return content;
    },
  });

  return service;
}

function escapeAlt(alt: string): string {
  return alt.replace(/([[\]])/g, '\\$1');
}

/**
 * Renders a table as a GFM pipe table, or null when it cannot be one.
 *
 * "Cannot" is deliberately strict: any `colspan`/`rowspan`, a nested table, or
 * rows of differing width all disqualify it. A pipe table that quietly drops a
 * merged cell looks correct and is not, which is worse than admitting the
 * table was flattened.
 */
function toPipeTable(table: DomElement): string | null {
  if (first(table.querySelector('table')) !== null) return null;

  const rows = toArray(table.querySelectorAll('tr'));
  if (rows.length === 0) return null;

  const grid: string[][] = [];
  for (const row of rows) {
    const cells = toArray(row.querySelectorAll('th, td'));
    if (cells.length === 0) continue;
    for (const cell of cells) {
      const span = attr(cell, 'colspan') || attr(cell, 'rowspan');
      if (span !== '' && span !== '1') return null;
    }
    grid.push(cells.map((cell) => text(cell).replace(/\|/g, '\\|')));
  }
  if (grid.length === 0) return null;

  const width = grid[0]?.length ?? 0;
  if (width === 0 || grid.some((row) => row.length !== width)) return null;

  // A table with no header row still needs one: GFM has no headerless form, so
  // an empty header is the honest rendering of "these columns are unnamed".
  const hasHeader = first(table.querySelector('th')) !== null;
  const header = hasHeader ? (grid.shift() ?? []) : Array.from({ length: width }, () => '');
  if (grid.length === 0 && !hasHeader) return null;

  const lines = [
    `| ${header.join(' | ')} |`,
    `| ${Array.from({ length: width }, () => '---').join(' | ')} |`,
    ...grid.map((row) => `| ${row.join(' | ')} |`),
  ];
  return lines.join('\n');
}
