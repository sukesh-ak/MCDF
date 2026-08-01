// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// EPUB import, mirroring `mcdf import-epub` on the desktop.
//
// EPUB is the one foreign format already structured like MCDF — a container of
// XHTML chapters, images and declared metadata — so the conversion is mostly a
// re-labelling: the spine becomes reading order, each chapter becomes a
// top-level heading carrying a `{#id}`, and the generated `schema.yaml` binds
// to those anchors. Structure comes out of structure rather than being
// recovered from layout.

import { checkMemberPath, ContainerError } from '../container/container.js';
import { emptyMetadata, type SchemaSection } from '../model/types.js';
import { compose } from './compose.js';
import { conversionNotes, convertDocument } from './html.js';
import { decodeHref, dirOf, resolvePath, slugify, stripFragment } from './paths.js';
import { newConversionState } from './turndown-setup.js';
import type { ImportOptions, ImportResult } from './types.js';
import { collapse, parseXmlElements } from './xml.js';
import { ZipArchive } from './zip.js';

const CONTAINER_XML = 'META-INF/container.xml';
const MIMETYPE = 'mimetype';

export class EpubImportError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EpubImportError';
  }
}

export async function importEpub(
  bytes: Uint8Array,
  options: ImportOptions,
): Promise<ImportResult> {
  const zip = ZipArchive.open(bytes);
  const notes: string[] = [];

  // The mimetype entry is required by the EPUB spec but is checked leniently:
  // plenty of real books get its storage rules wrong, and `container.xml` is
  // the entry that actually decides whether this is readable.
  const mimetype = zip.has(MIMETYPE) ? (await zip.readText(MIMETYPE))?.trim() : undefined;
  if (mimetype !== undefined && mimetype !== 'application/epub+zip') {
    notes.push(`the archive declares an unexpected media type (${mimetype})`);
  }

  const containerXml = await zip.readText(CONTAINER_XML);
  if (containerXml === null) {
    throw new EpubImportError(`not an EPUB: ${CONTAINER_XML} is missing`);
  }

  const rootfile = parseXmlElements(containerXml).find(
    (el) => el.name === 'rootfile' && (el.attrs['full-path'] ?? '') !== '',
  );
  const opfPath = rootfile?.attrs['full-path'] ?? '';
  if (opfPath === '') throw new EpubImportError('EPUB container.xml names no package file');

  // Decoded once, then used for both reading and as the base every chapter and
  // image resolves against. Deriving the directory from the raw form instead
  // would send a percent-encoded package path's chapters to the wrong place.
  const opfEntry = resolvePath('', decodeHref(opfPath));
  const opfSource = await zip.readText(opfEntry);
  if (opfSource === null) {
    throw new EpubImportError(`EPUB package file is missing: ${opfPath}`);
  }
  const opf = parseXmlElements(opfSource);
  const opfDir = dirOf(opfEntry);

  // -- Dublin Core metadata -------------------------------------------------

  const metadata = emptyMetadata();
  const authors: string[] = [];
  const firstText = (name: string): string => {
    for (const el of opf) {
      if (el.name !== name) continue;
      const value = collapse(el.text);
      if (value !== '') return value;
    }
    return '';
  };

  metadata.title = firstText('title');
  metadata.language = firstText('language');
  metadata.created_at = firstText('date');
  for (const el of opf) {
    if (el.name !== 'creator') continue;
    const who = collapse(el.text);
    if (who !== '') {
      metadata.authors.push({ name: who, id: '' });
      authors.push(who);
    }
  }

  // -- manifest (id -> href) and spine (reading order) ----------------------

  const items = new Map<string, { href: string; mediaType: string }>();
  for (const el of opf) {
    if (el.name !== 'item') continue;
    const id = el.attrs['id'] ?? '';
    const href = el.attrs['href'] ?? '';
    if (id === '' || href === '') continue;
    if (!items.has(id)) items.set(id, { href, mediaType: el.attrs['media-type'] ?? '' });
  }

  const spine: string[] = [];
  for (const el of opf) {
    if (el.name !== 'itemref') continue;
    const item = items.get(el.attrs['idref'] ?? '');
    if (item === undefined) continue;
    // Only readable documents; a spine can also reference SVG or fallbacks.
    if (item.mediaType !== '' && !item.mediaType.includes('html')) continue;
    const path = resolvePath(opfDir, decodeHref(stripFragment(item.href)));
    if (path !== '') spine.push(path);
  }
  if (spine.length === 0) {
    throw new EpubImportError('EPUB spine has no readable documents');
  }

  // -- chapters -------------------------------------------------------------

  const state = newConversionState();
  const sections: SchemaSection[] = [];
  const used = new Set<string>();
  const parts: string[] = [];
  let chapters = 0;
  let unreadable = 0;

  for (let i = 0; i < spine.length; i++) {
    const path = spine[i] ?? '';
    const xhtml = await zip.readText(path);
    if (xhtml === null) {
      unreadable++;
      continue;
    }

    const { title, markdown } = convertDocument(xhtml, dirOf(path), state);
    // A chapter with neither a title nor a body is a navigation stub or a
    // stylesheet holder; keeping it would put an empty section in the schema.
    if (markdown.trim() === '' && title === '') continue;

    const heading = title === '' ? `Chapter ${i + 1}` : title;
    let id = slugify(heading, `chapter-${i + 1}`);
    while (used.has(id)) id = `${id}-${i + 1}`;
    used.add(id);

    parts.push(`# ${heading} {#${id}}\n\n${markdown}\n`);
    sections.push({ id, title: heading, required: false });
    chapters++;
  }

  if (chapters === 0) throw new EpubImportError('EPUB produced no readable content');

  // -- images ---------------------------------------------------------------

  const assets = new Map<string, Uint8Array>();
  let missingImages = 0;
  for (const [source, target] of state.images) {
    let data: Uint8Array | null = null;
    try {
      data = await zip.read(source);
    } catch {
      // A member that will not inflate is a broken reference, not a reason to
      // lose the book.
      data = null;
    }
    if (data === null) {
      missingImages++;
      continue;
    }
    try {
      checkMemberPath(target);
    } catch (e) {
      // Cannot happen for a name this module generated, and is checked anyway:
      // the whole point of importing untrusted archives is that the safety
      // property must not rest on the generator being correct.
      if (e instanceof ContainerError) {
        missingImages++;
        continue;
      }
      throw e;
    }
    assets.set(target, data);
  }

  // -- compose --------------------------------------------------------------

  notes.push(...conversionNotes(state));
  notes.push('internal cross-references between chapters became plain text');
  if (zip.duplicates.length > 0) {
    notes.push(
      `the archive contains ${zip.duplicates.length} duplicate entry name` +
        `${zip.duplicates.length === 1 ? '' : 's'}; the first of each was used`,
    );
  }
  if (unreadable > 0) {
    notes.push(
      `${unreadable} chapter${unreadable === 1 ? '' : 's'} listed in the spine could not be read`,
    );
  }
  if (missingImages > 0) {
    notes.push(
      `${missingImages} image${missingImages === 1 ? '' : 's'} could not be extracted and ` +
        `${missingImages === 1 ? 'is' : 'are'} referenced but not included`,
    );
  }

  const { container, title } = compose(
    {
      format: 'epub',
      markdown: parts.join('\n'),
      metadata,
      sections,
      documentType: 'book',
      assets,
    },
    options,
  );

  return {
    container,
    report: {
      format: 'epub',
      source: options.source ?? '',
      title,
      authors,
      chapters,
      images: assets.size,
      notes,
    },
  };
}
