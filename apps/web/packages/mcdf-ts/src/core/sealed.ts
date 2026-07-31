// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Which members are ciphertext right now (spec §5.2: `encrypted_files` in
// `encryption/policy.yaml`).
//
// This lives apart from the rest of the encryption code on purpose. Loading,
// validating and rendering all have to know what is sealed *before* they touch
// a member, and none of them needs a key to find out — reading the list is
// plain YAML, so even a Core-profile implementation with no cryptography at all
// can, and must, consult it rather than parse ciphertext as Markdown.
//
// Keeping it here also keeps `document.ts` and `encrypt.ts` from importing each
// other, which they would otherwise have to.

import type { Container } from '../container/container.js';
import type { EncryptionPolicy } from '../model/types.js';
import { ENCRYPTION_POLICY_PATH, parseEncryptionPolicyYaml } from '../serialize/policy.js';

/** The policy a container carries, or `null` when it is not encrypted. */
export function readEncryptionPolicy(container: Container): EncryptionPolicy | null {
  if (!container.has(ENCRYPTION_POLICY_PATH)) return null;
  try {
    return parseEncryptionPolicyYaml(container.readText(ENCRYPTION_POLICY_PATH));
  } catch {
    // A policy too broken to parse is reported by `validate` as
    // E_POLICY_INVALID; callers of this helper only want to know what is
    // readable, and should not have to catch to find out.
    return null;
  }
}

/**
 * Members whose bytes are ciphertext.
 *
 * An editor needs this before it does anything else with a container: writing a
 * text buffer over a sealed member destroys it, and parsing one as YAML or
 * Markdown produces nonsense.
 */
export function encryptedMembers(container: Container): string[] {
  return readEncryptionPolicy(container)?.encrypted_files ?? [];
}

export function isSealed(container: Container, path: string): boolean {
  return encryptedMembers(container).includes(path);
}

/** The recipients a container is currently encrypted to, or `[]` if it is not. */
export function encryptionRecipients(container: Container): string[] {
  return readEncryptionPolicy(container)?.recipients.map((r) => r.id) ?? [];
}
