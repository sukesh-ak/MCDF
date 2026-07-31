// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Container signing, ported from `src/core/sign_ops.cpp`.
//
// Signatures cover the *canonical* manifest, not the bytes of `manifest.json`.
// That is what makes the guarantee legible: reformatting the file cannot break a
// signature, and changing any member's hash always does.

import type { Container } from '../container/container.js';
import { jwsSignDetached, jwsVerifyDetached } from '../crypto/jws.js';
import type { SigningKey } from '../crypto/keys.js';
import { canonicalizeJson } from '../serialize/jcs.js';
import { utf8Encode } from '../util/bytes.js';
import { MANIFEST_PATH } from './document.js';

export const SIGNATURE_DIR = 'signatures/';
const SIGNATURE_SUFFIX = '.sig';

export class SignError extends Error {
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'SignError';
    this.code = code;
  }
}

/** `signatures/<name>.sig` — where a detached JWS belongs. */
export function signaturePath(name: string): string {
  const stem = name.endsWith(SIGNATURE_SUFFIX)
    ? name.slice(0, -SIGNATURE_SUFFIX.length)
    : name;
  return `${SIGNATURE_DIR}${stem}${SIGNATURE_SUFFIX}`;
}

export function isSignaturePath(path: string): boolean {
  return path.startsWith(SIGNATURE_DIR) && path.endsWith(SIGNATURE_SUFFIX);
}

/** The bytes signatures are computed over: `manifest.json` in RFC 8785 form. */
export function canonicalManifestBytes(container: Container): Uint8Array {
  if (!container.has(MANIFEST_PATH)) {
    throw new SignError('manifest.json is missing', 'E_MISSING_MANIFEST');
  }
  return utf8Encode(canonicalizeJson(container.readText(MANIFEST_PATH)));
}

/**
 * Signs the container's canonical manifest. Returns the detached JWS; writing
 * it to `signatures/<name>.sig` is the caller's business, because the caller is
 * the one that knows whether the container is writable.
 */
export function signContainer(container: Container, key: SigningKey, kid?: string): string {
  return jwsSignDetached(key, canonicalManifestBytes(container), kid ?? key.didKey());
}

export interface SignatureCheck {
  /** Member path, e.g. `signatures/author.sig`. */
  file: string;
  valid: boolean;
  /** Signer's `did:key`, as claimed by the header. */
  kid: string;
  alg: string;
  /** Set when the signature could not be evaluated at all. */
  error: string;
  /** Normative code for the failure, when one applies. */
  code: string | undefined;
}

/**
 * Verifies every `signatures/*.sig` against the canonical manifest.
 *
 * `payload` overrides what they are checked against. An editor uses it to ask
 * the forward-looking question — "would these signatures still hold if I saved
 * now?" — by passing the manifest rebuilt from the live buffers. Verifying
 * against the stored manifest would keep reporting VALID while the document
 * changed underneath it, which is the opposite of what a trust panel is for.
 */
export function verifyContainer(container: Container, payload?: Uint8Array): SignatureCheck[] {
  const against = payload ?? canonicalManifestBytes(container);
  const checks: SignatureCheck[] = [];

  for (const file of container.list()) {
    if (!isSignaturePath(file)) continue;
    const verdict = jwsVerifyDetached(container.readText(file), against);
    checks.push({
      file,
      valid: verdict.valid,
      kid: verdict.kid,
      alg: verdict.alg,
      error: verdict.error,
      code: verdict.code,
    });
  }
  return checks;
}
