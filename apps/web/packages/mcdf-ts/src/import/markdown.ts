// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Markdown import — the trivial one, and the one that had to exist first.
//
// `content.md` *is* CommonMark, so there is no conversion here at all: the file
// becomes the document. The only real work is pulling referenced images into
// `assets/` so their links still resolve inside the container, which is what
// `mcdf create --from doc.md --assets dir` does on the desktop.

import { assetPath, decodeHref, isExternal, resolvePath, stripFragment } from './paths.js';
import { compose } from './compose.js';
import { emptyMetadata } from '../model/types.js';
import type { ImportOptions, ImportResult } from './types.js';

/**
 * Inline image references.
 *
 * The destination may be angle-bracketed, and an optional title follows it —
 * which for MCDF is also where layout hints live (`"width=600"`), so it is
 * captured and preserved rather than parsed.
 */
const IMAGE = /!\[([^\]]*)\]\(\s*(<[^>\n]*>|[^\s()]+)((?:\s+(?:"[^"]*"|'[^']*'|\([^)]*\)))?)\s*\)/g;

export interface MarkdownImportOptions extends ImportOptions {
  /**
   * Directory the source file sat in, so relative image paths resolve. Only
   * meaningful when the caller can supply assets at all.
   */
  baseDir?: string;
}

/**
 * Wraps a Markdown file as an MCDF document.
 *
 * No `schema.yaml` is generated, matching `create_document` on the desktop: a
 * plain Markdown file makes no structural claims, and inventing sections for it
 * would put text in the document that the author never wrote. The client offers
 * "build a schema from the content" as a separate, visible step.
 */
export async function importMarkdown(
  markdown: string,
  options: MarkdownImportOptions,
): Promise<ImportResult> {
  const baseDir = options.baseDir ?? '';
  const notes: string[] = [];
  const images = new Map<string, string>();
  const taken = new Set<string>();
  let unresolvable = 0;

  const rewritten = markdown.replace(IMAGE, (whole, alt: string, dest: string, title: string) => {
    const bare = dest.startsWith('<') && dest.endsWith('>') ? dest.slice(1, -1) : dest;
    if (bare === '' || isExternal(bare)) return whole;

    const source = resolvePath(baseDir, decodeHref(stripFragment(bare)));
    if (source === '') {
      unresolvable++;
      return whole;
    }
    let target = images.get(source);
    if (target === undefined) {
      target = assetPath(source, taken);
      images.set(source, target);
    }
    return `![${alt}](${target}${title})`;
  });

  const assets = new Map<string, Uint8Array>();
  let missing = 0;
  for (const [source, target] of images) {
    const data = await options.resolveAsset?.(source);
    // A broken image reference is reported, never fatal — the document is still
    // worth having, and the link is left pointing at the member that would have
    // been there so attaching it later repairs the document.
    if (data == null) {
      missing++;
      continue;
    }
    assets.set(target, data);
  }

  if (missing > 0) {
    notes.push(
      `${missing} referenced image${missing === 1 ? '' : 's'} could not be found and ` +
        `${missing === 1 ? 'was' : 'were'} not included — attach ${missing === 1 ? 'it' : 'them'} to repair the link${missing === 1 ? '' : 's'}`,
    );
  }
  if (unresolvable > 0) {
    notes.push(
      `${unresolvable} image reference${unresolvable === 1 ? '' : 's'} pointed outside the ` +
        `document's folder and ${unresolvable === 1 ? 'was' : 'were'} left as written`,
    );
  }

  const { container, title } = compose(
    {
      format: 'markdown',
      markdown: rewritten,
      metadata: emptyMetadata(),
      sections: [],
      documentType: '',
      assets,
    },
    options,
  );

  return {
    container,
    report: {
      format: 'markdown',
      source: options.source ?? '',
      title,
      authors: [],
      chapters: 1,
      images: assets.size,
      notes,
    },
  };
}
