// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import type { Container } from '../container/container.js';
import { MemoryContainer } from '../container/container.js';
import {
  emptyManifest,
  emptyMetadata,
  emptySchema,
  type McdfDocument,
} from '../model/types.js';
import { parseHeadings } from '../serialize/markdown.js';
import { encryptedMembers } from './sealed.js';
import {
  canonicalizeContent,
  metadataToYaml,
  parseMetadataYaml,
  parseSchemaYaml,
  schemaToYaml,
} from '../serialize/yaml.js';
import { buildManifest, manifestToCanonicalJson, parseManifestJson } from './manifest.js';

export const CONTENT_PATH = 'content.md';
export const SCHEMA_PATH = 'schema.yaml';
export const METADATA_PATH = 'metadata.yaml';
export const MANIFEST_PATH = 'manifest.json';

/**
 * Loads the standard members into an in-memory document view.
 *
 * Tolerant of partial containers — absent members leave the matching `has*`
 * flag false — because validation, not loading, is where a document is judged.
 */
export function loadDocument(container: Container): McdfDocument {
  // What is ciphertext has to be established before anything is parsed. Feeding
  // a sealed member to a YAML or CommonMark parser is not graceful degradation:
  // YAML throws and takes the whole load with it, while CommonMark cheerfully
  // produces headings that were never written (spec §5.2, §11).
  const sealed = new Set(encryptedMembers(container));

  const doc: McdfDocument = {
    metadata: emptyMetadata(),
    schema: emptySchema(),
    manifest: emptyManifest(),
    content: '',
    headings: [],
    sealed: [...sealed],
    hasMetadata: false,
    hasSchema: false,
    hasManifest: false,
    hasContent: false,
  };

  if (container.has(CONTENT_PATH)) {
    doc.hasContent = true;
    // The member exists either way; sealed just means it has no readable
    // structure, so `headings` stays empty rather than being invented.
    if (!sealed.has(CONTENT_PATH)) {
      doc.content = container.readText(CONTENT_PATH);
      doc.headings = parseHeadings(doc.content);
    }
  }
  if (container.has(SCHEMA_PATH) && !sealed.has(SCHEMA_PATH)) {
    doc.schema = parseSchemaYaml(container.readText(SCHEMA_PATH));
    doc.hasSchema = true;
  }
  if (container.has(METADATA_PATH) && !sealed.has(METADATA_PATH)) {
    doc.metadata = parseMetadataYaml(container.readText(METADATA_PATH));
    doc.hasMetadata = true;
  }
  if (container.has(MANIFEST_PATH)) {
    doc.manifest = parseManifestJson(container.readText(MANIFEST_PATH));
    doc.hasManifest = true;
  }
  return doc;
}

/**
 * Writes the document's editable members back into a container and rebuilds
 * `manifest.json` over the result.
 *
 * `content.md` is canonicalized on the way in, so saving twice without editing
 * produces identical bytes — the property the manifest depends on.
 */
export function saveDocument(
  container: MemoryContainer,
  doc: McdfDocument,
  options: { rebuildManifest?: boolean } = {},
): void {
  container.writeText(CONTENT_PATH, canonicalizeContent(doc.content));
  if (doc.hasSchema) container.writeText(SCHEMA_PATH, schemaToYaml(doc.schema));
  if (doc.hasMetadata) container.writeText(METADATA_PATH, metadataToYaml(doc.metadata));

  if (options.rebuildManifest ?? doc.hasManifest) {
    const manifest = buildManifest(container, doc.manifest.hash_algorithm);
    doc.manifest = manifest;
    doc.hasManifest = true;
    // Stored in canonical form with a trailing newline, matching the committed
    // vectors. The bytes on disk are free-form (manifest.json is excluded from
    // itself) — only the canonical serialization is normative, because that is
    // what signatures cover.
    container.writeText(MANIFEST_PATH, `${manifestToCanonicalJson(manifest)}\n`);
  }
}

/** A new, minimal Core-profile document. */
export function createDocument(title: string): MemoryContainer {
  const container = new MemoryContainer();
  container.writeText(
    CONTENT_PATH,
    canonicalizeContent(`# ${title} {#overview}\n\nStart writing here.\n`),
  );
  container.writeText(
    METADATA_PATH,
    metadataToYaml({
      title,
      version: '0.1.0',
      authors: [],
      created_at: '',
      classification: '',
      language: 'en',
    }),
  );
  container.writeText(
    SCHEMA_PATH,
    schemaToYaml({
      document_type: 'document',
      sections: [{ id: 'overview', title: 'Overview', required: true }],
    }),
  );
  return container;
}
