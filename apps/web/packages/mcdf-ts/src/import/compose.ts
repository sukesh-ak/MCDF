// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The last step every importer shares: turn converted Markdown, recovered
// metadata and collected assets into a real container.
//
// Kept in one place because the provenance rules are the part of importing that
// must not vary by source format — every import stamps `metadata.generated_by`
// and records an `IMPORTED` audit entry, and an importer that quietly skipped
// either would produce a document nobody could tell was converted.

import { MemoryContainer } from '../container/container.js';
import { auditAppend } from '../core/audit.js';
import {
  CONTENT_PATH,
  MANIFEST_PATH,
  METADATA_PATH,
  SCHEMA_PATH,
} from '../core/document.js';
import { buildManifest, manifestToCanonicalJson } from '../core/manifest.js';
import type { DocumentSchema, Metadata, SchemaSection } from '../model/types.js';
import { parseHeadings } from '../serialize/markdown.js';
import { canonicalizeContent, metadataToYaml, schemaToYaml } from '../serialize/yaml.js';
import { MCDF_TS_VERSION } from '../version.js';
import { DEFAULT_ACTOR, type ImportFormat, type ImportOptions } from './types.js';

/** The provenance stamp: which tool converted this, from what, at which version. */
export function generatedBy(format: ImportFormat): string {
  return `mcdf-ts ${format} import ${MCDF_TS_VERSION}`;
}

export interface ComposeInput {
  format: ImportFormat;
  markdown: string;
  metadata: Metadata;
  /** Written as `schema.yaml` when non-empty; omitted entirely otherwise. */
  sections: SchemaSection[];
  documentType: string;
  /** Container path -> bytes, already resolved and safety-checked by the caller. */
  assets: Map<string, Uint8Array>;
}

export interface ComposeResult {
  container: MemoryContainer;
  /** The title actually written, after the first-heading fallback below. */
  title: string;
}

/**
 * Assembles the container. The manifest is built last, so it covers the schema
 * and every asset.
 */
export function compose(input: ComposeInput, options: ImportOptions): ComposeResult {
  const container = new MemoryContainer();
  const content = canonicalizeContent(input.markdown);
  container.writeText(CONTENT_PATH, content);

  const metadata: Metadata = { ...input.metadata };
  // Same fallback the reference implementation's `create_document` applies: a
  // document with no declared title is named by its first heading, because a
  // library screen showing "untitled" for a book called "Moby-Dick" is worse
  // than a guess that is almost always right.
  if (metadata.title === '') {
    metadata.title = parseHeadings(content)[0]?.text ?? '';
  }
  metadata.generated_by = generatedBy(input.format);
  container.writeText(METADATA_PATH, metadataToYaml(metadata));

  if (input.sections.length > 0) {
    const schema: DocumentSchema = {
      document_type: input.documentType,
      sections: input.sections,
    };
    container.writeText(SCHEMA_PATH, schemaToYaml(schema));
  }

  for (const [path, data] of input.assets) container.write(path, data);

  // Before the manifest, though `audit.log` is excluded from it either way —
  // the ordering states the intent that the manifest is the final act.
  auditAppend(container, 'IMPORTED', options.actor ?? DEFAULT_ACTOR, options.now);

  const manifest = buildManifest(container);
  container.writeText(MANIFEST_PATH, `${manifestToCanonicalJson(manifest)}\n`);
  return { container, title: metadata.title };
}
