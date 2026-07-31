// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The interoperability tests that matter: signatures produced by the C++
// reference implementation must verify here, byte for byte, with no shared code.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import { MemoryContainer } from '../container/container.js';
import { SigningKey } from '../crypto/keys.js';
import { utf8Decode } from '../util/bytes.js';
import { createDocument, loadDocument, saveDocument } from './document.js';
import { buildManifest, manifestToCanonicalJson } from './manifest.js';
import { canonicalManifestBytes, signContainer, signaturePath, verifyContainer } from './sign.js';
import { validate } from './validate.js';

const HERE = fileURLToPath(new URL('.', import.meta.url));
const KIT = join(HERE, '..', '..', '..', '..', '..', '..', 'conformance');

function vector(name: string): MemoryContainer {
  const dir = join(KIT, 'vectors', name, 'container');
  const container = new MemoryContainer();
  const files = ['content.md', 'manifest.json', 'metadata.yaml', 'signatures/author.sig'];
  for (const file of files) {
    try {
      container.write(file, new Uint8Array(readFileSync(join(dir, file))));
    } catch {
      // Vectors carry different member sets; absence is part of the case.
    }
  }
  return container;
}

/** Rebuilds `manifest.json` in place, as saving does. */
function rebuild(container: MemoryContainer): void {
  container.writeText('manifest.json', `${manifestToCanonicalJson(buildManifest(container))}\n`);
}

describe('canonical signing payload', () => {
  it('re-serializes the stored manifest rather than trusting its formatting', () => {
    const container = new MemoryContainer();
    container.writeText('content.md', 'hi\n');
    // Same content, deliberately ugly formatting and reversed key order.
    container.writeText(
      'manifest.json',
      '{\n  "hash_algorithm" : "sha256",\n  "mcdf_version": "1.0",\n  "files": {}\n}\n',
    );
    expect(utf8Decode(canonicalManifestBytes(container))).toBe(
      '{"files":{},"hash_algorithm":"sha256","mcdf_version":"1.0"}',
    );
  });

  it('refuses to sign a container with no manifest', () => {
    const container = new MemoryContainer();
    container.writeText('content.md', 'hi\n');
    expect(() => canonicalManifestBytes(container)).toThrow(/manifest\.json is missing/);
  });
});

describe('reference-implementation signatures', () => {
  it('verifies the signature in valid/encrypted, produced by the C++ CLI', () => {
    const container = vector('valid/encrypted');
    const checks = verifyContainer(container);
    expect(checks).toHaveLength(1);
    expect(checks[0]?.file).toBe('signatures/author.sig');
    expect(checks[0]?.alg).toBe('EdDSA');
    expect(checks[0]?.valid).toBe(true);
  });

  it('rejects the stale signature in invalid/sig-invalid', () => {
    const checks = verifyContainer(vector('invalid/sig-invalid'));
    expect(checks[0]?.valid).toBe(false);
    // A stale signature is cryptographically wrong, not unevaluable.
    expect(checks[0]?.error).toBe('');
  });
});

describe('sign and verify a container', () => {
  it.each(['EdDSA', 'ES256'] as const)('%s: round-trips through the Signed profile', (algorithm) => {
    const key = SigningKey.generate(algorithm);
    const container = createDocument('Signed Document');
    rebuild(container);

    container.writeText(signaturePath('author'), signContainer(container, key));

    const checks = verifyContainer(container);
    expect(checks).toHaveLength(1);
    expect(checks[0]?.valid).toBe(true);
    expect(checks[0]?.kid).toBe(key.didKey());
    expect(checks[0]?.alg).toBe(algorithm);

    const report = validate(container, loadDocument(container), 'signed');
    expect(report.issues).toEqual([]);
    expect(report.ok).toBe(true);
  });

  it('is invalidated by an edit, and repaired by re-signing', () => {
    const key = SigningKey.generate('EdDSA');
    const container = createDocument('Tamper Demo');
    rebuild(container);
    container.writeText(signaturePath('author'), signContainer(container, key));
    expect(validate(container, loadDocument(container), 'signed').ok).toBe(true);

    // Edit the content and rebuild the manifest: integrity is restored, but the
    // signature covers the *old* manifest, so trust is not. This is the whole
    // demo — a document that cannot quietly change under a signature.
    const doc = loadDocument(container);
    doc.content = '# Tamper Demo {#overview}\n\nEdited after signing.\n';
    saveDocument(container, doc, { rebuildManifest: true });

    const broken = validate(container, loadDocument(container), 'signed');
    expect(broken.ok).toBe(false);
    expect(broken.issues.map((i) => i.code)).toEqual(['E_SIG_INVALID']);
    // Integrity alone still passes — the manifest matches the new bytes.
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);

    container.writeText(signaturePath('author'), signContainer(container, key));
    expect(validate(container, loadDocument(container), 'signed').ok).toBe(true);
  });

  it('carries multiple signers, and reports exactly the one that broke', () => {
    const alice = SigningKey.generate('EdDSA');
    const bob = SigningKey.generate('ES256');
    const container = createDocument('Countersigned');
    rebuild(container);

    container.writeText(signaturePath('alice'), signContainer(container, alice));
    container.writeText(signaturePath('bob'), signContainer(container, bob));
    expect(validate(container, loadDocument(container), 'signed').ok).toBe(true);

    // Corrupt only Alice's signature.
    container.writeText(signaturePath('alice'), signContainer(container, alice).slice(0, -4) + 'AAAA');
    const report = validate(container, loadDocument(container), 'signed');
    expect(report.issues).toHaveLength(1);
    expect(report.issues[0]?.message.startsWith('signatures/alice.sig')).toBe(true);
  });

  it('does not let a signature change the manifest it signs', () => {
    // signatures/ is excluded from the manifest, so adding one must not make
    // the container's own integrity check fail.
    const key = SigningKey.generate('EdDSA');
    const container = createDocument('Excluded');
    rebuild(container);
    const before = utf8Decode(canonicalManifestBytes(container));

    container.writeText(signaturePath('author'), signContainer(container, key));
    rebuild(container);

    expect(utf8Decode(canonicalManifestBytes(container))).toBe(before);
    expect(verifyContainer(container)[0]?.valid).toBe(true);
  });
});
