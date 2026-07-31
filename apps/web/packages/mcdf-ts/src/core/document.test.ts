// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { MemoryContainer } from '../container/container.js';
import {
  canonicalizeContent,
  metadataToYaml,
  parseMetadataYaml,
  parseSchemaYaml,
  schemaToYaml,
} from '../serialize/yaml.js';
import { createDocument, loadDocument, saveDocument } from './document.js';
import { buildManifest, verifyManifest } from './manifest.js';
import { validate } from './validate.js';

describe('createDocument', () => {
  it('produces a container that passes the Core profile', () => {
    const container = createDocument('Test Document');
    const doc = loadDocument(container);
    expect(validate(container, doc, 'core').ok).toBe(true);
    expect(doc.metadata.title).toBe('Test Document');
    expect(doc.headings[0]?.id).toBe('overview');
  });

  it('fails Integrity until a manifest is built', () => {
    const container = createDocument('Test');
    const report = validate(container, loadDocument(container), 'integrity');
    expect(report.issues.map((i) => i.code)).toContain('E_MISSING_MANIFEST');
  });
});

describe('saveDocument', () => {
  it('rebuilds a manifest that verifies', () => {
    const container = createDocument('Test');
    const doc = loadDocument(container);
    saveDocument(container, doc, { rebuildManifest: true });

    const reloaded = loadDocument(container);
    expect(verifyManifest(container, reloaded.manifest).ok).toBe(true);
    expect(validate(container, reloaded, 'integrity').ok).toBe(true);
  });

  it('is byte-stable across repeated saves', () => {
    const container = createDocument('Test');
    saveDocument(container, loadDocument(container), { rebuildManifest: true });
    const first = container.toTar();

    const again = MemoryContainer.fromTar(first);
    saveDocument(again, loadDocument(again), { rebuildManifest: true });
    expect(again.toTar()).toEqual(first);
  });

  it('breaks the manifest when content changes without a rebuild', () => {
    const container = createDocument('Test');
    saveDocument(container, loadDocument(container), { rebuildManifest: true });

    container.writeText('content.md', '# Tampered {#overview}\n');
    const report = validate(container, loadDocument(container), 'integrity');
    expect(report.issues.map((i) => i.code)).toContain('E_MANIFEST_HASH_MISMATCH');
  });
});

describe('manifest membership', () => {
  it('excludes the manifest, audit log and signatures', () => {
    const container = createDocument('Test');
    container.writeText('audit.log', 'x\n');
    container.writeText('signatures/a.sig', 'x');
    container.writeText('manifest.json', '{}');
    expect(Object.keys(buildManifest(container)).length).toBeGreaterThan(0);
    expect(Object.keys(buildManifest(container).files).sort()).toEqual([
      'content.md',
      'metadata.yaml',
      'schema.yaml',
    ]);
  });

  it('is not fooled by a member named like an Object prototype key', () => {
    const container = createDocument('Test');
    container.writeText('toString', 'x');
    saveDocument(container, loadDocument(container), { rebuildManifest: true });
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);
  });
});

describe('canonicalizeContent', () => {
  it('normalises line endings and trailing blank lines', () => {
    expect(canonicalizeContent('a\r\nb\r\n\n\n')).toBe('a\nb\n');
    expect(canonicalizeContent('a')).toBe('a\n');
    expect(canonicalizeContent('')).toBe('');
  });
});

describe('schema YAML', () => {
  it('omits an empty document_type rather than writing an empty string', () => {
    // Spec §4.2: document_type is optional and MUST NOT be present-but-empty.
    // `document_type: ""` fails schema.schema.json's minLength, so emitting it
    // would make this writer produce output the conformance kit rejects.
    // Exact bytes, not just "does not contain": tests/test_writers.cpp asserts
    // this identical string, so a divergence in either writer breaks one of the
    // two. The manifest hashes what is on disk, which makes emission a
    // byte-level contract between implementations, not a formatting preference.
    const yaml = schemaToYaml({ document_type: '', sections: [] });
    expect(yaml).toBe('sections: []\n');
  });

  it('round-trips a schema with no document_type, byte-stably', () => {
    const schema = {
      document_type: '',
      sections: [{ id: 'intro', title: 'Introduction', required: true }],
    };
    const yaml = schemaToYaml(schema);
    expect(yaml).not.toContain('document_type');

    const back = parseSchemaYaml(yaml);
    expect(back.document_type).toBe('');
    expect(back.sections).toEqual(schema.sections);
    expect(schemaToYaml(back)).toBe(yaml);
  });

  it('still writes a document_type that is set', () => {
    expect(schemaToYaml({ document_type: 'contract', sections: [] })).toContain(
      'document_type: contract',
    );
  });
});

describe('metadata YAML', () => {
  it('round-trips', () => {
    const yaml = metadataToYaml({
      title: 'A Title',
      version: '1.0.0',
      authors: [{ name: 'Ada', id: 'did:key:z6Mk' }],
      created_at: '2026-07-29',
      classification: 'public',
      language: 'en',
    });
    const back = parseMetadataYaml(yaml);
    expect(back.title).toBe('A Title');
    expect(back.authors).toEqual([{ name: 'Ada', id: 'did:key:z6Mk' }]);
    expect(back.created_at).toBe('2026-07-29');
  });

  it('quotes scalars a bare YAML reader would re-type', () => {
    const yaml = metadataToYaml({
      title: 'yes',
      version: '1.0',
      authors: [],
      created_at: '2026-07-29',
      classification: '',
      language: 'en',
    });
    expect(yaml).toContain('title: "yes"');
    expect(yaml).toContain('version: "1.0"');
    expect(parseMetadataYaml(yaml).title).toBe('yes');
    expect(parseMetadataYaml(yaml).version).toBe('1.0');
  });
});
