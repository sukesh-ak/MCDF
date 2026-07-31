// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The canonical render (spec §10.4): HTML and plain text whose bytes are
// identical in every implementation, not merely across runs of this one.
//
// That distinction is the whole reason this file exists. §6 only ever required
// a renderer to be reproducible against itself, which no second implementation
// can be scored on — and the reference renderer stamped its own version into
// every page, so byte-parity was impossible by construction. §10.4 fixes the
// output shape and names the *format* version instead, and this module produces
// it independently: micromark parses the CommonMark, and everything around it is
// written from the spec.

import type { Container } from '../container/container.js';
import { sha256Hex } from '../crypto/hash.js';
import { encryptedMembers } from './sealed.js';
import { canonicalManifestBytes } from './sign.js';
import { parseMetadataYaml } from '../serialize/yaml.js';
import { renderHtml, renderText } from '../serialize/markdown.js';

const CONTENT_PATH = 'content.md';
const METADATA_PATH = 'metadata.yaml';
const MANIFEST_PATH = 'manifest.json';

export const RENDER_FORMATS = ['html', 'text'] as const;
export type RenderFormat = (typeof RENDER_FORMATS)[number];

/**
 * Version of the canonical render *format*, not of this library: every
 * implementation of canonical render N emits the same bytes for the same
 * container. Bump only when the output shape changes — 2 added heading id
 * attributes, which 1 dropped.
 */
export const CANONICAL_RENDER_VERSION = 2;

/** Title used when the container carries no metadata title. */
const DEFAULT_TITLE = 'MCDF Document';

export class RenderError extends Error {
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'RenderError';
    this.code = code;
  }
}

export function isRenderFormat(value: string): value is RenderFormat {
  return (RENDER_FORMATS as readonly string[]).includes(value);
}

function htmlEscape(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/**
 * Renders a container's `content.md` in the canonical form of §10.4.
 *
 * Refuses a sealed `content.md` rather than rendering it: a CommonMark parser
 * handed ciphertext does not fail, it produces confident nonsense with headings
 * nobody wrote (spec §6, §11).
 */
export function renderCanonical(container: Container, format: RenderFormat): string {
  if (!container.has(CONTENT_PATH)) {
    throw new RenderError('content.md is missing', 'E_MISSING_CONTENT');
  }
  if (encryptedMembers(container).includes(CONTENT_PATH)) {
    throw new RenderError(
      'content.md is encrypted; decrypt the document before rendering it',
      'E_CONTENT_SEALED',
    );
  }

  const content = container.readText(CONTENT_PATH);
  if (format === 'text') return `${renderText(content)}\n`;

  let title = DEFAULT_TITLE;
  if (container.has(METADATA_PATH) && !encryptedMembers(container).includes(METADATA_PATH)) {
    const parsed = parseMetadataYaml(container.readText(METADATA_PATH));
    if (parsed.title !== '') title = parsed.title;
  }

  // Provenance that a second implementation can also produce: the hash of the
  // canonical manifest identifies the container this page came from, and is the
  // same number whichever renderer computed it.
  let sourceHash = '';
  if (container.has(MANIFEST_PATH)) {
    sourceHash = sha256Hex(canonicalManifestBytes(container));
  }

  const head = [
    '<!DOCTYPE html>',
    '<html lang="en">',
    '<head>',
    '<meta charset="utf-8">',
    '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; ' +
      "style-src 'unsafe-inline'; img-src data:;\">",
    `<title>${htmlEscape(title)}</title>`,
    `<meta name="generator" content="mcdf-render/${CANONICAL_RENDER_VERSION}">`,
  ];
  if (sourceHash !== '') {
    head.push(`<meta name="mcdf-source" content="sha256:${sourceHash}">`);
  }
  head.push(
    '<style>body{font-family:system-ui,-apple-system,sans-serif;' +
      'max-width:44rem;margin:2rem auto;padding:0 1rem;line-height:1.6}' +
      'pre{overflow-x:auto}</style>',
    '</head>',
    '<body>',
  );

  return `${head.join('\n')}\n${renderHtml(content)}</body>\n</html>\n`;
}
