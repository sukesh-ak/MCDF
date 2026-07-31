// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import { MemoryContainer } from '../container/container.js';
import { EncPrivateKey, EncPublicKey } from '../crypto/enc-keys.js';
import { SigningKey } from '../crypto/keys.js';
import { emptyEncryptionPolicy } from '../model/types.js';
import {
  ENCRYPTION_POLICY_PATH,
  encryptionPolicyToYaml,
  parseEncryptionPolicyYaml,
} from '../serialize/policy.js';
import { schemaToYaml } from '../serialize/yaml.js';
import { utf8Decode } from '../util/bytes.js';
import { createDocument, loadDocument } from './document.js';
import { buildManifest, manifestToCanonicalJson } from './manifest.js';
import { decryptContainer, encryptContainer } from './encrypt.js';
import { readEncryptionPolicy } from './sealed.js';
import { signContainer, signaturePath, verifyContainer } from './sign.js';
import { validate } from './validate.js';

const HERE = fileURLToPath(new URL('.', import.meta.url));
const KIT = join(HERE, '..', '..', '..', '..', '..', '..', 'conformance');

function rebuild(container: MemoryContainer): void {
  container.writeText('manifest.json', `${manifestToCanonicalJson(buildManifest(container))}\n`);
}

/**
 * A document shaped like the reference `valid/encrypted` vector: content and
 * metadata, no `schema.yaml` — so no structure attestation is required.
 *
 * Encrypting a document that *does* carry a schema is the interesting case and
 * has its own block below.
 */
function confidentialDocument(title: string, body = 'Board pack, Q3.'): MemoryContainer {
  const container = new MemoryContainer();
  container.writeText('content.md', `# ${title} {#overview}\n\n${body}\n`);
  container.writeText('metadata.yaml', `title: ${title}\nversion: 0.1.0\nauthors: []\n`);
  rebuild(container);
  return container;
}

/** Content plus a schema whose sections do or do not bind, as asked. */
function schemaDocument(sections: { id: string; required: boolean }[], anchors: string[]) {
  const container = new MemoryContainer();
  container.writeText(
    'content.md',
    `# Title {#${anchors[0] ?? 'none'}}\n\nBody.\n` +
      anchors.slice(1).map((a) => `\n## Section {#${a}}\n\nMore.\n`).join(''),
  );
  container.writeText(
    'schema.yaml',
    schemaToYaml({
      document_type: 'document',
      sections: sections.map((s) => ({ id: s.id, title: s.id, required: s.required })),
    }),
  );
  rebuild(container);
  return container;
}

describe('encryption/policy.yaml', () => {
  // Parity that is cheap to state and expensive to lose: the policy is hashed
  // by the manifest, so a stray space here changes a signature's payload. Both
  // vectors were written by the C++ implementation, so re-emitting them
  // unchanged is a genuine cross-implementation check, not a self-round-trip.
  it.each([
    ['valid/encrypted', 'without a structure attestation'],
    ['valid/encrypted-schema', 'with a structure attestation'],
  ])('re-emits %s byte for byte (%s)', (vectorPath) => {
    const [kind, name] = vectorPath.split('/') as [string, string];
    const original = readFileSync(
      join(KIT, 'vectors', kind, name, 'container', 'encryption', 'policy.yaml'),
      'utf8',
    );
    expect(encryptionPolicyToYaml(parseEncryptionPolicyYaml(original))).toBe(original);
  });

  it('reads the fields the Encrypted profile checks', () => {
    const policy = parseEncryptionPolicyYaml(
      readFileSync(
        join(KIT, 'vectors', 'valid', 'encrypted', 'container', 'encryption', 'policy.yaml'),
        'utf8',
      ),
    );
    expect(policy.method).toBe('aes-256-gcm');
    expect(policy.key_management).toBe('hpke');
    expect(policy.encrypted_files).toEqual(['content.md']);
    expect(policy.recipients).toHaveLength(1);
    expect(EncPublicKey.fromDidKey(policy.recipients[0]?.id as string).raw.length).toBe(32);
  });
});

describe('X25519 recipient keys', () => {
  it('round-trips through did:key', () => {
    const key = EncPrivateKey.generate();
    const did = key.didKey();
    expect(did.startsWith('did:key:z6LS')).toBe(true); // x25519 multicodec prefix
    expect(EncPublicKey.fromDidKey(did).raw).toEqual(key.publicKey().raw);
  });

  it('rejects a signing did:key as a recipient', () => {
    const signer = SigningKey.generate('EdDSA');
    expect(() => EncPublicKey.fromDidKey(signer.didKey())).toThrow(/not an x25519 key/);
  });
});

describe('encrypt and decrypt', () => {
  it('round-trips content through AES-256-GCM with an HPKE-wrapped key', async () => {
    const recipient = EncPrivateKey.generate();
    const container = confidentialDocument('Confidential');
    const plaintext = container.read('content.md');

    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    // The bytes on disk are no longer the document.
    expect(container.read('content.md')).not.toEqual(plaintext);
    expect(container.has(ENCRYPTION_POLICY_PATH)).toBe(true);
    // ...and the manifest hashes the ciphertext, so Integrity still holds.
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);

    // The Encrypted profile sits above Signed in the ladder, so it also wants a
    // signature — which is why the kit's own encrypted vector carries one.
    container.writeText(
      signaturePath('author'),
      signContainer(container, SigningKey.generate('EdDSA')),
    );
    expect(validate(container, loadDocument(container), 'encrypted').ok).toBe(true);

    const decrypted = await decryptContainer(container, recipient);
    expect(decrypted).toEqual(['content.md']);
    expect(container.read('content.md')).toEqual(plaintext);
    expect(container.has(ENCRYPTION_POLICY_PATH)).toBe(false);
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);
  });

  it('encrypts to several recipients, any one of whom can open it', async () => {
    const alice = EncPrivateKey.generate();
    const bob = EncPrivateKey.generate();
    const container = createDocument('Shared Secret');
    rebuild(container);
    const plaintext = container.read('content.md');

    await encryptContainer(container, ['content.md'], [alice.publicKey(), bob.publicKey()]);

    for (const recipient of [alice, bob]) {
      const copy = container.clone();
      await decryptContainer(copy, recipient);
      expect(copy.read('content.md')).toEqual(plaintext);
    }
  });

  it('refuses a key that is not a recipient', async () => {
    const recipient = EncPrivateKey.generate();
    const stranger = EncPrivateKey.generate();
    const container = createDocument('Not For You');
    rebuild(container);
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    await expect(decryptContainer(container, stranger)).rejects.toThrow(/not a recipient/);
  });

  it('binds a ciphertext to its member path', async () => {
    // The AAD is the path, so a ciphertext relocated to another member must
    // fail authentication instead of decrypting somewhere it does not belong.
    const recipient = EncPrivateKey.generate();
    const container = createDocument('Bound');
    container.writeText('notes.md', 'a second member\n');
    rebuild(container);
    await encryptContainer(container, ['content.md', 'notes.md'], [recipient.publicKey()]);

    container.write('notes.md', container.read('content.md'));
    await expect(decryptContainer(container, recipient)).rejects.toThrow(/authentication failed/);
  });

  it('refuses to encrypt an already-encrypted container', async () => {
    const recipient = EncPrivateKey.generate();
    const container = createDocument('Once Only');
    rebuild(container);
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);
    await expect(
      encryptContainer(container, ['content.md'], [recipient.publicKey()]),
    ).rejects.toThrow(/already encrypted/);
  });

  it('invalidates a signature made before encryption', async () => {
    // Encrypting changes the bytes, so it changes the manifest, so it breaks
    // the signature. Signing again after encrypting is the supported order.
    const signer = SigningKey.generate('EdDSA');
    const recipient = EncPrivateKey.generate();
    const container = confidentialDocument('Sign Then Encrypt');
    container.writeText(signaturePath('author'), signContainer(container, signer));
    expect(verifyContainer(container)[0]?.valid).toBe(true);

    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);
    expect(verifyContainer(container)[0]?.valid).toBe(false);

    container.writeText(signaturePath('author'), signContainer(container, signer));
    expect(validate(container, loadDocument(container), 'encrypted').ok).toBe(true);
  });

  it('leaves nothing recognisable in the ciphertext', async () => {
    const recipient = EncPrivateKey.generate();
    const container = confidentialDocument('Secret');
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);
    expect(utf8Decode(container.read('content.md'))).not.toContain('Board pack');
  });

  it('produces a different ciphertext every time (fresh nonce per seal)', async () => {
    const recipient = EncPrivateKey.generate();
    const [a, b] = [confidentialDocument('Nonce'), confidentialDocument('Nonce')];
    await encryptContainer(a, ['content.md'], [recipient.publicKey()]);
    await encryptContainer(b, ['content.md'], [recipient.publicKey()]);
    expect(a.read('content.md')).not.toEqual(b.read('content.md'));
  });
});

describe('the structure attestation (spec §5.2.1)', () => {
  it('records which sections bound, so an encrypted document still validates', async () => {
    // The gap this closes: before the attestation existed, both implementations
    // reported a required section as *missing* once content.md was sealed,
    // because ciphertext has no headings. Encrypting a document cost it its
    // conformance.
    const recipient = EncPrivateKey.generate();
    const container = createDocument('Schema And Secrets');
    rebuild(container);
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);

    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    expect(readEncryptionPolicy(container)?.structure).toEqual({
      present: true,
      bound_sections: ['overview'],
    });
    expect(validate(container, loadDocument(container), 'core').issues).toEqual([]);
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);

    await decryptContainer(container, recipient);
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);
  });

  it('still reports a section that genuinely did not bind', async () => {
    // The rule does not soften, it just gets its input from elsewhere: a
    // required section with no heading is an error sealed or not.
    const recipient = EncPrivateKey.generate();
    const container = schemaDocument(
      [
        { id: 'overview', required: true },
        { id: 'terms', required: true },
      ],
      ['overview'],
    );
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    expect(readEncryptionPolicy(container)?.structure.bound_sections).toEqual(['overview']);
    const report = validate(container, loadDocument(container), 'core');
    expect(report.issues.map((i) => i.code)).toEqual(['E_REQUIRED_SECTION_MISSING']);
    expect(report.issues[0]?.message).toContain('terms');
  });

  it('is omitted when there is no schema to corroborate it', async () => {
    const recipient = EncPrivateKey.generate();
    const container = confidentialDocument('No Schema');
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    expect(readEncryptionPolicy(container)?.structure.present).toBe(false);
    expect(container.readText(ENCRYPTION_POLICY_PATH)).not.toContain('structure:');
  });

  it('is omitted when schema.yaml is itself sealed, so the ids do not leak', async () => {
    const recipient = EncPrivateKey.generate();
    const container = schemaDocument([{ id: 'overview', required: true }], ['overview']);
    await encryptContainer(
      container,
      ['content.md', 'schema.yaml'],
      [recipient.publicKey()],
    );

    const policy = readEncryptionPolicy(container);
    expect(policy?.structure.present).toBe(false);
    // The ids are only safe to publish because schema.yaml publishes them; with
    // the schema sealed too, listing them would give away what was hidden.
    expect(container.readText(ENCRYPTION_POLICY_PATH)).not.toContain('overview');
  });

  it('records an empty list rather than nothing when no section bound', async () => {
    // "No section bound" and "no claim made" are different states; collapsing
    // them would let an encryptor hide a broken document by staying silent.
    const recipient = EncPrivateKey.generate();
    const container = schemaDocument([{ id: 'terms', required: false }], ['other']);
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    expect(readEncryptionPolicy(container)?.structure).toEqual({
      present: true,
      bound_sections: [],
    });
    expect(container.readText(ENCRYPTION_POLICY_PATH)).toContain('  bound_sections: []\n');
    expect(validate(container, loadDocument(container), 'core').issues.map((i) => i.code)).toEqual([
      'E_SCHEMA_UNBOUND',
    ]);
  });

  it('cannot be evaluated when it is missing, and says so once', async () => {
    // Not "every required section is missing" — that blames the document for
    // the encryptor's omission.
    const recipient = EncPrivateKey.generate();
    const container = schemaDocument(
      [
        { id: 'overview', required: true },
        { id: 'terms', required: true },
      ],
      ['overview', 'terms'],
    );
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    const policy = parseEncryptionPolicyYaml(container.readText(ENCRYPTION_POLICY_PATH));
    policy.structure = { present: false, bound_sections: [] };
    container.writeText(ENCRYPTION_POLICY_PATH, encryptionPolicyToYaml(policy));
    rebuild(container);

    const report = validate(container, loadDocument(container), 'core');
    expect(report.issues.map((i) => i.code)).toEqual(['E_POLICY_INVALID']);
  });

  it('is rejected when it claims something about content that is not sealed', async () => {
    const container = schemaDocument([{ id: 'overview', required: true }], ['overview']);
    const policy = emptyEncryptionPolicy();
    policy.encrypted_files = ['metadata.yaml'];
    policy.structure = { present: true, bound_sections: ['overview'] };
    policy.recipients = [
      { id: EncPrivateKey.generate().didKey(), enc: 'AA', wrapped_key: 'AA' },
    ];
    container.writeText('metadata.yaml', 'title: x\n');
    container.writeText(ENCRYPTION_POLICY_PATH, encryptionPolicyToYaml(policy));
    rebuild(container);

    const report = validate(container, loadDocument(container), 'encrypted');
    expect(report.issues.map((i) => i.code)).toContain('E_POLICY_INVALID');
  });

  it('is caught by the first recipient who opens the document', async () => {
    // The attestation is a claim, not a proof — this is what stops it being
    // merely decorative. A hostile encryptor can write a false list; it does
    // not survive contact with someone holding the key.
    const recipient = EncPrivateKey.generate();
    const container = schemaDocument([{ id: 'terms', required: true }], ['other']);
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    const policy = parseEncryptionPolicyYaml(container.readText(ENCRYPTION_POLICY_PATH));
    policy.structure.bound_sections = ['terms']; // a lie: nothing bound
    container.writeText(ENCRYPTION_POLICY_PATH, encryptionPolicyToYaml(policy));
    rebuild(container);

    // While sealed, the lie is believed — that is the honest limit of the design.
    expect(validate(container, loadDocument(container), 'core').issues).toEqual([]);

    await expect(decryptContainer(container, recipient)).rejects.toThrow(
      /does not match the structure recorded/,
    );
  });
});

describe('sealed members are not parsed', () => {
  it('leaves a sealed content.md unparsed rather than inventing headings', async () => {
    const recipient = EncPrivateKey.generate();
    const container = confidentialDocument('Opaque');
    await encryptContainer(container, ['content.md'], [recipient.publicKey()]);

    const doc = loadDocument(container);
    expect(doc.sealed).toEqual(['content.md']);
    expect(doc.hasContent).toBe(true); // the member is there...
    expect(doc.headings).toEqual([]); // ...but nothing in it is readable
    expect(doc.content).toBe('');
  });

  it('survives a sealed schema.yaml instead of failing the whole load', async () => {
    // Ciphertext through a YAML parser throws, and taking the entire document
    // load down with it would make an encrypted container unopenable by tools
    // that only wanted to read its manifest.
    const recipient = EncPrivateKey.generate();
    const container = schemaDocument([{ id: 'overview', required: true }], ['overview']);
    await encryptContainer(container, ['schema.yaml'], [recipient.publicKey()]);

    const doc = loadDocument(container);
    expect(doc.hasSchema).toBe(false); // a schema nobody can read makes no claims
    expect(validate(container, doc, 'core').issues).toEqual([]);
  });
});
