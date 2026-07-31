// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Store behaviour, including the tamper demo the a11y story is built around.

import { MemoryContainer, loadDocument, validate } from 'mcdf-ts';
import { describe, expect, it } from 'vitest';

import { DocumentStore } from './document.svelte.ts';

function withManifest(): DocumentStore {
  const store = new DocumentStore();
  store.addManifest();
  return store;
}

describe('DocumentStore', () => {
  it('starts on a Core-valid document', () => {
    const store = new DocumentStore();
    store.profile = 'core';
    expect(store.report.ok).toBe(true);
    expect(store.dirty).toBe(false);
  });

  it('tracks the outline as content changes', () => {
    const store = new DocumentStore();
    store.content = '# One {#overview}\n\n## Two {#details}\n';
    expect(store.headings.map((h) => h.id)).toEqual(['overview', 'details']);
  });

  it('reports an unbound required section', () => {
    const store = new DocumentStore();
    store.content = '# Renamed\n';
    expect(store.unboundSections.map((s) => s.id)).toEqual(['overview']);
    expect(store.report.issues.map((i) => i.code)).toContain('E_REQUIRED_SECTION_MISSING');
  });

  it('becomes dirty on edit and clean again after packing', () => {
    const store = withManifest();
    expect(store.dirty).toBe(false);
    store.content = '# Overview {#overview}\n\nEdited.\n';
    expect(store.dirty).toBe(true);
    store.pack();
    expect(store.dirty).toBe(false);
  });

  it('breaks manifest integrity the moment content changes — the tamper demo', () => {
    const store = withManifest();
    expect(store.manifestUpToDate).toBe(true);
    expect(store.members.every((m) => m.state !== 'modified')).toBe(true);

    store.content = '# Overview {#overview}\n\nTampered.\n';

    expect(store.manifestUpToDate).toBe(false);
    expect(store.members.find((m) => m.path === 'content.md')?.state).toBe('modified');
    expect(store.report.issues.map((i) => i.code)).toContain('E_MANIFEST_HASH_MISMATCH');
  });

  it('marks an attached asset as unlisted until the manifest is rebuilt', () => {
    const store = withManifest();
    store.attach('assets/logo.svg', new TextEncoder().encode('<svg/>'));

    expect(store.members.find((m) => m.path === 'assets/logo.svg')?.state).toBe('unlisted');
    expect(store.report.issues.map((i) => i.code)).toContain('E_MANIFEST_EXTRA_FILE');

    store.addManifest();
    expect(store.manifestUpToDate).toBe(true);
    expect(store.report.ok).toBe(true);
  });

  it('excludes the audit log and signatures from manifest coverage', () => {
    const store = withManifest();
    store.attach('audit.log', new TextEncoder().encode('x\n'));
    store.attach('signatures/a.sig', new TextEncoder().encode('x'));

    const states = new Map(store.members.map((m) => [m.path, m.state]));
    expect(states.get('audit.log')).toBe('excluded');
    expect(states.get('signatures/a.sig')).toBe('excluded');
    expect(store.report.ok).toBe(true);
  });

  it('packs bytes that mcdf-ts reads back as a valid document', () => {
    const store = withManifest();
    store.content = '# Overview {#overview}\n\nBody text.\n';
    const bytes = store.pack();

    const container = MemoryContainer.fromTar(bytes);
    const report = validate(container, loadDocument(container), 'integrity');
    expect(report.issues).toEqual([]);
  });

  it('packs deterministically — the same document twice is the same bytes', () => {
    const store = withManifest();
    store.content = '# Overview {#overview}\n\nStable.\n';
    const first = store.pack();
    const second = store.pack();
    expect(second).toEqual(first);
  });

  it('canonicalizes content on save so line endings never change the hash', () => {
    const store = withManifest();
    store.content = '# Overview {#overview}\r\n\r\nCRLF.\r\n\r\n\r\n';
    const bytes = store.pack();

    const container = MemoryContainer.fromTar(bytes);
    expect(container.readText('content.md')).toBe('# Overview {#overview}\n\nCRLF.\n');
  });
});
