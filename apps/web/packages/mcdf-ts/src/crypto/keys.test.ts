// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { utf8Encode } from '../util/bytes.js';
import { base58btcDecode, base58btcEncode, base64urlDecode, base64urlEncode } from './encoding.js';
import { jwsSignDetached, jwsVerifyDetached } from './jws.js';
import { KeyError, SigningKey, VerifyKey } from './keys.js';

/** The signer used throughout the committed conformance vectors. */
const VECTOR_DID = 'did:key:z6MkhhBx46hod8VKm6RZs9Gf7FDgMDFeSvXLZqRSu8BpoaLU';

describe('base64url', () => {
  // RFC 4648 §10 test vectors, unpadded as JWS requires.
  it.each([
    ['', ''],
    ['f', 'Zg'],
    ['fo', 'Zm8'],
    ['foo', 'Zm9v'],
    ['foob', 'Zm9vYg'],
    ['fooba', 'Zm9vYmE'],
    ['foobar', 'Zm9vYmFy'],
  ])('encodes %o as %o', (plain, encoded) => {
    expect(base64urlEncode(utf8Encode(plain))).toBe(encoded);
    expect(base64urlDecode(encoded)).toEqual(utf8Encode(plain));
  });

  it('uses the URL-safe alphabet, never + or /', () => {
    const bytes = Uint8Array.of(0xfb, 0xff, 0xbe);
    const encoded = base64urlEncode(bytes);
    expect(encoded).not.toMatch(/[+/=]/);
    expect(base64urlDecode(encoded)).toEqual(bytes);
  });

  it('rejects a character outside the alphabet', () => {
    expect(() => base64urlDecode('ab*d')).toThrow(/invalid base64url/);
  });
});

describe('base58btc', () => {
  it('preserves leading zero bytes as leading ones', () => {
    const bytes = Uint8Array.of(0, 0, 1, 2, 3);
    expect(base58btcEncode(bytes).startsWith('11')).toBe(true);
    expect(base58btcDecode(base58btcEncode(bytes))).toEqual(bytes);
  });

  it('round-trips 34 random bytes (a multicodec-prefixed key)', () => {
    for (let run = 0; run < 20; run++) {
      const bytes = new Uint8Array(34).map(() => Math.floor(Math.random() * 256));
      expect(base58btcDecode(base58btcEncode(bytes))).toEqual(bytes);
    }
  });
});

describe('did:key', () => {
  it('resolves the Ed25519 did:key used by the conformance vectors', () => {
    const key = VerifyKey.fromDidKey(VECTOR_DID);
    expect(key.algorithm).toBe('EdDSA');
    expect(key.publicKey.length).toBe(32);
    // Re-encoding must reproduce the exact string the reference emitted.
    expect(key.didKey()).toBe(VECTOR_DID);
  });

  it('round-trips both key types', () => {
    for (const algorithm of ['EdDSA', 'ES256'] as const) {
      const key = SigningKey.generate(algorithm);
      const did = key.didKey();
      expect(did.startsWith('did:key:z')).toBe(true);
      expect(VerifyKey.fromDidKey(did).algorithm).toBe(algorithm);
      expect(VerifyKey.fromDidKey(did).didKey()).toBe(did);
    }
  });

  it('reports an unresolvable kid with its normative code', () => {
    for (const bad of ['', 'did:web:example.com', 'did:key:zNotBase58!', 'did:key:z6Mk']) {
      let caught: KeyError | undefined;
      try {
        VerifyKey.fromDidKey(bad);
      } catch (e) {
        caught = e as KeyError;
      }
      expect(caught?.code, bad).toBe('E_KID_UNRESOLVABLE');
    }
  });

  it('rejects an X25519 did:key as a signing key', () => {
    // Multicodec 0xec01 is a key-agreement key; using one to verify a signature
    // is the mistake this guard exists for.
    const x25519Did = 'did:key:z6LSerLwKkreS9zTk25eKt3eXC32Bm7mqsahk3z6WYHSDFBm';
    expect(() => VerifyKey.fromDidKey(x25519Did)).toThrow(/unsupported did:key type/);
  });
});

describe('signing', () => {
  const payload = utf8Encode('{"files":{},"hash_algorithm":"sha256","mcdf_version":"1.0"}');

  it.each(['EdDSA', 'ES256'] as const)('%s signs and verifies', (algorithm) => {
    const key = SigningKey.generate(algorithm);
    const signature = key.sign(payload);
    expect(key.publicKey().verify(payload, signature)).toBe(true);
    expect(key.publicKey().verify(utf8Encode('other'), signature)).toBe(false);
  });

  it.each(['EdDSA', 'ES256'] as const)('%s round-trips through PKCS#8 PEM', (algorithm) => {
    const key = SigningKey.generate(algorithm);
    const pem = key.toPkcs8Pem();
    expect(pem.startsWith('-----BEGIN PRIVATE KEY-----\n')).toBe(true);
    expect(pem.endsWith('-----END PRIVATE KEY-----\n')).toBe(true);

    const reloaded = SigningKey.fromPkcs8Pem(pem);
    expect(reloaded.algorithm).toBe(algorithm);
    expect(reloaded.didKey()).toBe(key.didKey());
    // A signature from the reloaded key must verify under the original's DID —
    // the property that makes a key file portable between implementations.
    expect(VerifyKey.fromDidKey(key.didKey()).verify(payload, reloaded.sign(payload))).toBe(true);
  });

  it('rejects a PEM that is not a private key', () => {
    expect(() => SigningKey.fromPkcs8Pem('not a pem')).toThrow(/PRIVATE KEY/);
  });
});

describe('detached JWS', () => {
  const payload = utf8Encode('the canonical manifest');

  it.each(['EdDSA', 'ES256'] as const)('%s produces a verifiable detached JWS', (algorithm) => {
    const key = SigningKey.generate(algorithm);
    const jws = jwsSignDetached(key, payload, key.didKey());

    const [header, empty, signature] = jws.split('.');
    expect(empty).toBe('');
    expect(header).toBeTruthy();
    expect(signature).toBeTruthy();

    const verdict = jwsVerifyDetached(jws, payload);
    expect(verdict.valid).toBe(true);
    expect(verdict.alg).toBe(algorithm);
    expect(verdict.kid).toBe(key.didKey());
  });

  it('writes the header keys in the order the reference does', () => {
    const key = SigningKey.generate('EdDSA');
    const jws = jwsSignDetached(key, payload, key.didKey());
    const header = new TextDecoder().decode(base64urlDecode(jws.split('.')[0] as string));
    expect(header).toBe(`{"alg":"EdDSA","kid":"${key.didKey()}"}`);
  });

  it('fails a signature over different bytes', () => {
    const key = SigningKey.generate('EdDSA');
    const jws = jwsSignDetached(key, payload, key.didKey());
    const verdict = jwsVerifyDetached(jws, utf8Encode('tampered'));
    expect(verdict.valid).toBe(false);
    expect(verdict.error).toBe('');
  });

  it('rejects an alg outside the allow-list', () => {
    const header = base64urlEncode(utf8Encode(JSON.stringify({ alg: 'RS256', kid: VECTOR_DID })));
    const verdict = jwsVerifyDetached(`${header}..bm90LWEtcmVhbC1zaWduYXR1cmU`, payload);
    expect(verdict.valid).toBe(false);
    expect(verdict.code).toBe('E_SIG_ALG_NOT_ALLOWED');
  });

  it('rejects an alg that does not match the resolved key (confusion guard)', () => {
    const ed = SigningKey.generate('EdDSA');
    // Claim ES256 over an Ed25519 kid: without the guard, a verifier could be
    // steered into the wrong primitive.
    const header = base64urlEncode(utf8Encode(JSON.stringify({ alg: 'ES256', kid: ed.didKey() })));
    const verdict = jwsVerifyDetached(`${header}..${base64urlEncode(ed.sign(payload))}`, payload);
    expect(verdict.valid).toBe(false);
    expect(verdict.code).toBe('E_SIG_ALG_MISMATCH');
  });

  it('rejects a non-detached JWS', () => {
    const key = SigningKey.generate('EdDSA');
    const jws = jwsSignDetached(key, payload, key.didKey()).replace('..', `.${base64urlEncode(payload)}.`);
    expect(jwsVerifyDetached(jws, payload).code).toBe('E_SIG_INVALID');
  });
});
