// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// AES-256-GCM, ported from `src/crypto/aead.cpp`. WebCrypto does the work —
// it is native everywhere this runs, hardware-accelerated, and (unlike Ed25519)
// uniformly supported, so there is no reason to ship an implementation.
//
// Sealed layout is `nonce (12) || ciphertext || tag (16)`, matching the
// reference byte for byte. WebCrypto already returns ciphertext‖tag joined, so
// the only framing here is the nonce prefix.

const NONCE_LENGTH = 12;
const TAG_LENGTH = 16;
const KEY_LENGTH = 32;

/**
 * The slice of WebCrypto this module uses, declared structurally.
 *
 * `mcdf-ts` compiles without the DOM lib on purpose — that is what stops a
 * `document.` or a `window.` from creeping into the core. Naming
 * the four members actually needed keeps that guarantee while still type-checking
 * the calls, and it doubles as documentation of the platform surface a host has
 * to provide.
 */
interface AesGcmParams {
  name: 'AES-GCM';
  iv: Uint8Array;
  additionalData: Uint8Array;
  tagLength: number;
}

interface SubtleCryptoLike {
  importKey(
    format: 'raw',
    keyData: Uint8Array,
    algorithm: 'AES-GCM',
    extractable: boolean,
    keyUsages: string[],
  ): Promise<unknown>;
  encrypt(algorithm: AesGcmParams, key: unknown, data: Uint8Array): Promise<ArrayBuffer>;
  decrypt(algorithm: AesGcmParams, key: unknown, data: Uint8Array): Promise<ArrayBuffer>;
}

interface CryptoLike {
  getRandomValues<T extends Uint8Array>(array: T): T;
  subtle: SubtleCryptoLike;
}

export class AeadError extends Error {
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'AeadError';
    this.code = code;
  }
}

function webcrypto(): CryptoLike {
  const c = (globalThis as { crypto?: CryptoLike }).crypto;
  if (c?.subtle === undefined) {
    throw new AeadError(
      'WebCrypto is unavailable. In a browser this means the page is not a secure context (HTTPS).',
    );
  }
  return c;
}

/** Cryptographically strong random bytes — content-encryption keys and nonces. */
export function randomBytes(n: number): Uint8Array {
  const out = new Uint8Array(n);
  webcrypto().getRandomValues(out);
  return out;
}

async function importKey(key: Uint8Array, usage: 'encrypt' | 'decrypt'): Promise<unknown> {
  if (key.length !== KEY_LENGTH) {
    throw new AeadError(`AES-256-GCM key must be ${KEY_LENGTH} bytes, got ${key.length}`);
  }
  return webcrypto().subtle.importKey('raw', key.slice(), 'AES-GCM', false, [usage]);
}

/**
 * Seals `plaintext` under `key`, binding it to `aad` (the member's path, so a
 * ciphertext moved to another member no longer decrypts).
 */
export async function aes256gcmSeal(
  key: Uint8Array,
  aad: string,
  plaintext: Uint8Array,
): Promise<Uint8Array> {
  const nonce = randomBytes(NONCE_LENGTH);
  const sealed = new Uint8Array(
    await webcrypto().subtle.encrypt(
      { name: 'AES-GCM', iv: nonce, additionalData: new TextEncoder().encode(aad), tagLength: TAG_LENGTH * 8 },
      await importKey(key, 'encrypt'),
      plaintext.slice(),
    ),
  );

  const out = new Uint8Array(NONCE_LENGTH + sealed.length);
  out.set(nonce, 0);
  out.set(sealed, NONCE_LENGTH);
  return out;
}

export async function aes256gcmOpen(
  key: Uint8Array,
  aad: string,
  sealed: Uint8Array,
): Promise<Uint8Array> {
  if (sealed.length < NONCE_LENGTH + TAG_LENGTH) {
    throw new AeadError('ciphertext too short', 'E_DECRYPT_FAILED');
  }
  const nonce = sealed.subarray(0, NONCE_LENGTH);
  const body = sealed.subarray(NONCE_LENGTH);

  try {
    return new Uint8Array(
      await webcrypto().subtle.decrypt(
        { name: 'AES-GCM', iv: nonce, additionalData: new TextEncoder().encode(aad), tagLength: TAG_LENGTH * 8 },
        await importKey(key, 'decrypt'),
        body.slice(),
      ),
    );
  } catch {
    // WebCrypto reports every failure as the same opaque OperationError, which
    // is the correct thing for it to do: distinguishing "wrong key" from
    // "tampered ciphertext" would be an oracle.
    throw new AeadError(`authentication failed for ${aad}`, 'E_DECRYPT_FAILED');
  }
}
