// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Detached JWS (RFC 7515 appendix F) in compact form, ported from
// `src/crypto/jws.cpp`. The payload segment is empty — "header..signature" —
// because the payload is the canonical manifest, which already lives in the
// container. Signing a copy of it would let the two drift.

import { utf8Encode } from '../util/bytes.js';
import { base64urlDecode, base64urlEncode } from './encoding.js';
import { KeyError, VerifyKey, isSignatureAlgorithm, type SigningKey } from './keys.js';

export class JwsError extends Error {
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'JwsError';
    this.code = code;
  }
}

/**
 * The protected header. Key order is `alg` then `kid` and must stay that way:
 * the header is base64url-encoded verbatim into the signing input, so the exact
 * bytes are part of what is signed. The reference implementation emits the same
 * order (nlohmann's object is key-sorted, and "alg" < "kid").
 */
function protectedHeader(alg: string, kid: string): string {
  return base64urlEncode(utf8Encode(JSON.stringify({ alg, kid })));
}

function signingInput(headerB64: string, payload: Uint8Array): Uint8Array {
  return utf8Encode(`${headerB64}.${base64urlEncode(payload)}`);
}

/** Signs `payload` and returns the detached compact serialization. */
export function jwsSignDetached(key: SigningKey, payload: Uint8Array, kid: string): string {
  const header = protectedHeader(key.algorithm, kid);
  const signature = key.sign(signingInput(header, payload));
  return `${header}..${base64urlEncode(signature)}`;
}

export interface JwsVerification {
  valid: boolean;
  alg: string;
  kid: string;
  /** Why the JWS could not be checked at all; empty when it simply failed. */
  error: string;
  /** Normative code for the failure, when one applies. */
  code: string | undefined;
}

function unverifiable(error: string, code?: string, alg = '', kid = ''): JwsVerification {
  return { valid: false, alg, kid, error, code };
}

/**
 * Verifies a detached JWS over `payload`, resolving the key from the header's
 * `kid`. Never throws: every failure is a structured verdict, because callers
 * are validating untrusted containers and need to tell "wrong signature" from
 * "signature I could not evaluate".
 */
export function jwsVerifyDetached(compact: string, payload: Uint8Array): JwsVerification {
  const parts = compact.trim().split('.');
  if (parts.length !== 3 || parts[1] !== '') {
    return unverifiable('malformed detached JWS (expected "header..signature")', 'E_SIG_INVALID');
  }
  const [headerB64, , signatureB64] = parts as [string, string, string];

  let alg = '';
  let kid = '';
  try {
    const header = JSON.parse(new TextDecoder().decode(base64urlDecode(headerB64))) as unknown;
    if (typeof header !== 'object' || header === null) throw new Error('header is not an object');
    const h = header as Record<string, unknown>;
    alg = typeof h['alg'] === 'string' ? h['alg'] : '';
    kid = typeof h['kid'] === 'string' ? h['kid'] : '';
  } catch (e) {
    return unverifiable(`JWS header: ${(e as Error).message}`, 'E_SIG_INVALID');
  }

  if (!isSignatureAlgorithm(alg)) {
    return unverifiable(`unsupported JWS alg: ${alg || '(absent)'}`, 'E_SIG_ALG_NOT_ALLOWED', alg, kid);
  }

  let key: VerifyKey;
  try {
    key = VerifyKey.fromDidKey(kid);
  } catch (e) {
    const err = e as KeyError;
    return unverifiable(err.message, err.code ?? 'E_KID_UNRESOLVABLE', alg, kid);
  }

  // Algorithm-confusion guard: the header may not claim an algorithm the
  // resolved key cannot speak.
  if (key.algorithm !== alg) {
    return unverifiable(
      `JWS alg ${alg} does not match the key type ${key.algorithm}`,
      'E_SIG_ALG_MISMATCH',
      alg,
      kid,
    );
  }

  let signature: Uint8Array;
  try {
    signature = base64urlDecode(signatureB64);
  } catch (e) {
    return unverifiable(`JWS signature: ${(e as Error).message}`, 'E_SIG_INVALID', alg, kid);
  }

  return {
    valid: key.verify(signingInput(headerB64, payload), signature),
    alg,
    kid,
    error: '',
    code: undefined,
  };
}
