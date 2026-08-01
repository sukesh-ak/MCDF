// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Turning converted Markdown into MCDF's document shape: exactly one H1 per
// source document, carrying the `{#id}` anchor a `schema.yaml` section binds
// to, with everything the source called a heading pushed one level down.
//
// The reference implementation does this by editing the parsed tree before
// converting (`xhtml_md.hpp`: remove the heading that repeats the title, then
// emit every heading one level deeper). Doing it on the Markdown afterwards
// reaches the same shape without needing a DOM at this layer — which is what
// lets the whole importer run in Node, a worker and a browser alike.

import { collapse, decodeEntities } from './xml.js';

/** The `<title>` of an HTML or XHTML document, collapsed, or ''. */
export function extractTitle(source: string): string {
  const match = /<title\b[^>]*>([\s\S]*?)<\/title\s*>/i.exec(source);
  if (match === null) return '';
  // Strip any stray markup inside the title before decoding, so a
  // `<title>A <b>B</b></title>` yields "A B" rather than "A <b>B</b>".
  return collapse(decodeEntities((match[1] ?? '').replace(/<[^>]*>/g, ' ')));
}

interface Line {
  text: string;
  /** True inside a fenced code block, where `#` is content and not a heading. */
  fenced: boolean;
}

/**
 * Splits into lines, marking which sit inside a fenced code block.
 *
 * Without this, a shell snippet's `# comment` is a heading: it would be
 * demoted, counted, and could even be mistaken for the document title.
 */
function scan(markdown: string): Line[] {
  const out: Line[] = [];
  let fence = '';
  for (const text of markdown.split('\n')) {
    const open = /^\s{0,3}(`{3,}|~{3,})/.exec(text);
    if (fence === '') {
      const marker = open?.[1];
      if (marker !== undefined) {
        fence = marker[0] ?? '';
        out.push({ text, fenced: true });
        continue;
      }
      out.push({ text, fenced: false });
      continue;
    }
    out.push({ text, fenced: true });
    // A fence closes on a run of the same character, at least as long as the
    // opener's — approximated by "same character", which is what matters here.
    if (open?.[1]?.startsWith(fence) === true) fence = '';
  }
  return out;
}

const ATX = /^(\s{0,3})(#{1,6})(\s+.*)?$/;

/**
 * If the document opens with a heading, returns its text and the rest.
 *
 * Used to avoid emitting the chapter title twice: the source's own leading
 * heading and the H1 the importer writes are the same heading, and keeping
 * both produces a document that says everything twice.
 */
export function takeLeadingHeading(markdown: string): { title: string; rest: string } {
  const lines = scan(markdown);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line === undefined) continue;
    if (line.text.trim() === '') continue;
    if (line.fenced) break;
    const match = ATX.exec(line.text);
    if (match === null) break;
    const title = collapse((match[3] ?? '').replace(/\s+#+\s*$/, ''));
    if (title === '') break;
    const rest = lines
      .slice(i + 1)
      .map((l) => l.text)
      .join('\n');
    return { title, rest: rest.replace(/^\n+/, '') };
  }
  return { title: '', rest: markdown };
}

/**
 * Pushes every heading one level down, capping at H6.
 *
 * The importer emits the single H1 that carries the schema anchor, so a source
 * H1 has to become an H2 or the document ends up with several top-level
 * headings and no clear title. H6 has nowhere to go and stays H6 — the
 * alternative, dropping it, would lose a heading outright.
 */
export function demoteHeadings(markdown: string): string {
  return scan(markdown)
    .map(({ text, fenced }) => {
      if (fenced) return text;
      const match = ATX.exec(text);
      if (match === null) return text;
      const indent = match[1] ?? '';
      const hashes = match[2] ?? '';
      const rest = match[3] ?? '';
      return `${indent}${'#'.repeat(Math.min(6, hashes.length + 1))}${rest}`;
    })
    .join('\n');
}

/** Trims blank lines from both ends without touching interior spacing. */
export function trimBlankLines(markdown: string): string {
  return markdown.replace(/^\s*\n/, '').replace(/\s+$/, '');
}
