// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Confidential documents, ported from `src/core/encrypt_ops.cpp`.
//
// One content-encryption key seals every listed member with AES-256-GCM; the
// CEK itself is wrapped to each recipient with HPKE. The manifest is rebuilt
// afterwards so it hashes what is actually on disk — ciphertext after
// encrypting, plaintext after decrypting. A signature made before encryption
// therefore stops verifying, which is correct: the container's bytes changed.

import type { WritableContainer } from '../container/container.js';
import { aes256gcmOpen, aes256gcmSeal, randomBytes } from '../crypto/aead.js';
import { HPKE_INFO, hpkeSeal, type EncPrivateKey, type EncPublicKey } from '../crypto/enc-keys.js';
import { base64urlDecode, base64urlEncode } from '../crypto/encoding.js';
import {
  DEFAULT_ENCRYPTION_METHOD,
  DEFAULT_KEY_MANAGEMENT,
  emptyEncryptionPolicy,
  type EncryptionPolicy,
} from '../model/types.js';
import { parseHeadings } from '../serialize/markdown.js';
import {
  ENCRYPTION_POLICY_PATH,
  encryptionPolicyToYaml,
  parseEncryptionPolicyYaml,
} from '../serialize/policy.js';
import { parseSchemaYaml } from '../serialize/yaml.js';
import { buildManifest, manifestToCanonicalJson } from './manifest.js';

const MANIFEST_PATH = 'manifest.json';
const CONTENT_PATH = 'content.md';
const SCHEMA_PATH = 'schema.yaml';

export class EncryptError extends Error {
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'EncryptError';
    this.code = code;
  }
}

/** Rewrites `manifest.json` over whatever the members now contain. */
function rebuildManifest(container: WritableContainer): void {
  const manifest = buildManifest(container);
  container.writeText(MANIFEST_PATH, `${manifestToCanonicalJson(manifest)}\n`);
}

/**
 * Which schema sections bind to a heading in the given content.
 *
 * This is the one computation the attestation records and the one `decrypt`
 * re-runs, so both call it rather than each reimplementing §4.2. Order follows
 * the schema, which makes the comparison on decrypt exact: a reordered list is
 * as much a mismatch as a missing one.
 */
function boundSections(schemaYaml: string, content: string): string[] {
  const schema = parseSchemaYaml(schemaYaml);
  // Top-level headings only, exactly as validate binds them (spec §4.2). These
  // two must not drift: an encryptor that attested a nested anchor would record
  // a binding no validator would accept, and the document would fail the moment
  // it was sealed.
  const anchors = new Set(
    parseHeadings(content)
      .filter((h) => h.topLevel)
      .map((h) => h.id)
      .filter((id) => id !== ''),
  );
  return schema.sections.filter((s) => s.id !== '' && anchors.has(s.id)).map((s) => s.id);
}

/** True when spec §5.2.1 requires an attestation for this seal. */
function attestationRequired(container: WritableContainer, files: string[]): boolean {
  if (!files.includes(CONTENT_PATH)) return false;
  if (files.includes(SCHEMA_PATH)) return false; // would leak the section ids
  if (!container.has(SCHEMA_PATH)) return false;
  return parseSchemaYaml(container.readText(SCHEMA_PATH)).sections.length > 0;
}

/**
 * Encrypts `files` in place and writes `encryption/policy.yaml`.
 *
 * The CEK never leaves this function: it is generated here, used here, and only
 * its HPKE-wrapped forms are persisted.
 */
export async function encryptContainer(
  container: WritableContainer,
  files: string[],
  recipients: EncPublicKey[],
): Promise<EncryptionPolicy> {
  if (files.length === 0) throw new EncryptError('no files to encrypt');
  if (recipients.length === 0) throw new EncryptError('no recipients');
  if (container.has(ENCRYPTION_POLICY_PATH)) {
    // Encrypting twice would seal ciphertext under a second CEK and leave the
    // first policy unopenable — a silent way to destroy a document.
    throw new EncryptError(
      'this container is already encrypted; decrypt it before encrypting again',
      'E_POLICY_INVALID',
    );
  }

  const policy = emptyEncryptionPolicy();
  policy.method = DEFAULT_ENCRYPTION_METHOD;
  policy.key_management = DEFAULT_KEY_MANAGEMENT;
  policy.encrypted_files = [...files];

  // The attestation has to be computed BEFORE anything is sealed: once
  // content.md is ciphertext its headings are gone (spec §5.2.1).
  if (attestationRequired(container, files)) {
    policy.structure = {
      present: true,
      bound_sections: boundSections(
        container.readText(SCHEMA_PATH),
        container.readText(CONTENT_PATH),
      ),
    };
  }

  const cek = randomBytes(32);

  for (const file of files) {
    if (!container.has(file)) throw new EncryptError(`no such file: ${file}`);
    // The member path is the AAD, so a ciphertext copied to a different member
    // fails authentication rather than decrypting into the wrong place.
    container.write(file, await aes256gcmSeal(cek, file, container.read(file)));
  }
  for (const recipient of recipients) {
    const sealed = await hpkeSeal(recipient, HPKE_INFO, cek);
    policy.recipients.push({
      id: recipient.didKey(),
      enc: base64urlEncode(sealed.enc),
      wrapped_key: base64urlEncode(sealed.ct),
    });
  }

  container.writeText(ENCRYPTION_POLICY_PATH, encryptionPolicyToYaml(policy));
  rebuildManifest(container);
  return policy;
}

/** Decrypts every member the policy lists, then removes the policy. */
export async function decryptContainer(
  container: WritableContainer,
  key: EncPrivateKey,
): Promise<string[]> {
  if (!container.has(ENCRYPTION_POLICY_PATH)) {
    throw new EncryptError(`${ENCRYPTION_POLICY_PATH} not found`, 'E_POLICY_INVALID');
  }
  const policy = parseEncryptionPolicyYaml(container.readText(ENCRYPTION_POLICY_PATH));

  const mine = policy.recipients.find((r) => r.id === key.didKey());
  if (mine === undefined) {
    throw new EncryptError('this key is not a recipient of the document', 'E_NOT_A_RECIPIENT');
  }

  let cek: Uint8Array;
  try {
    cek = await key.hpkeOpen(
      base64urlDecode(mine.enc),
      base64urlDecode(mine.wrapped_key),
      HPKE_INFO,
    );
  } catch (e) {
    throw new EncryptError(`cannot unwrap the content key: ${(e as Error).message}`, 'E_DECRYPT_FAILED');
  }

  for (const file of policy.encrypted_files) {
    if (!container.has(file)) {
      throw new EncryptError(`encrypted file listed in policy is missing: ${file}`, 'E_POLICY_INVALID');
    }
    container.write(file, await aes256gcmOpen(cek, file, container.read(file)));
  }

  // Now that the headings are readable, hold the attestation to account. This
  // is what makes it more than a note to self: a claim that does not match the
  // content it sealed fails at the first recipient who opens the document
  // (spec §5.2.1).
  if (policy.structure.present && container.has(SCHEMA_PATH) && container.has(CONTENT_PATH)) {
    const actual = boundSections(
      container.readText(SCHEMA_PATH),
      container.readText(CONTENT_PATH),
    );
    const claimed = policy.structure.bound_sections;
    if (actual.length !== claimed.length || actual.some((id, i) => id !== claimed[i])) {
      throw new EncryptError(
        'the sealed content does not match the structure recorded in ' +
          `${ENCRYPTION_POLICY_PATH} (claimed [${claimed.join(', ')}], found [${actual.join(', ')}])`,
        'E_STRUCTURE_ATTESTATION_INVALID',
      );
    }
  }

  container.remove(ENCRYPTION_POLICY_PATH);
  rebuildManifest(container);
  return [...policy.encrypted_files];
}

