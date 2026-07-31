// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Signing keys and `did:key`, ported from `src/crypto/keys.cpp`.
//
// The spec's signature allow-list is EdDSA (Ed25519) and ES256 (ECDSA P-256).
// Both come from @noble/curves rather than WebCrypto: Ed25519 support in
// WebCrypto is still uneven across browsers, and a verifier that
// works in one browser and not another would make signature checking a
// user-agent lottery. @noble is audited, zero-dependency, and identical in Node,
// the browser and a worker — which also keeps `validate()` synchronous.

import { ed25519 } from '@noble/curves/ed25519';
import { p256 } from '@noble/curves/p256';
import { sha256 } from '@noble/hashes/sha256';

import {
  DerReader,
  OID_EC_PUBLIC_KEY,
  OID_ED25519,
  OID_PRIME256V1,
  TAG_INTEGER,
  TAG_OCTET_STRING,
  TAG_OID,
  TAG_SEQUENCE,
  bitString,
  bytesMatch,
  contextTag,
  octetString,
  oid,
  pemDecode,
  pemEncode,
  sequence,
  smallInteger,
} from './der.js';
import { base58btcDecode, base58btcEncode } from './encoding.js';

/** The JWS `alg` values MCDF permits. */
export type SignatureAlgorithm = 'EdDSA' | 'ES256';

export const SIGNATURE_ALGORITHMS: readonly SignatureAlgorithm[] = ['EdDSA', 'ES256'];

export function isSignatureAlgorithm(alg: string): alg is SignatureAlgorithm {
  return alg === 'EdDSA' || alg === 'ES256';
}

export class KeyError extends Error {
  /**
   * Normative code, when the failure has one. The reference implementation
   * classifies these by matching on its own error strings; carrying the code on
   * the error instead means a reworded message can never silently reclassify a
   * validation issue.
   */
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'KeyError';
    this.code = code;
  }
}

/** Multicodec prefixes (unsigned varint) for the public key types in play. */
const MULTICODEC_ED25519_PUB = Uint8Array.of(0xed, 0x01);
const MULTICODEC_P256_PUB = Uint8Array.of(0x80, 0x24);

const DID_KEY_PREFIX = 'did:key:z';

/** `did:key:z…` from a multicodec prefix and raw public key bytes. */
export function encodeDidKey(multicodec: Uint8Array, publicKey: Uint8Array): string {
  const bytes = new Uint8Array(multicodec.length + publicKey.length);
  bytes.set(multicodec, 0);
  bytes.set(publicKey, multicodec.length);
  return DID_KEY_PREFIX + base58btcEncode(bytes);
}

export interface DecodedDidKey {
  multicodec: Uint8Array;
  publicKey: Uint8Array;
}

/** Splits a `did:key` into its multicodec prefix and raw key bytes. */
export function decodeDidKey(did: string): DecodedDidKey {
  if (!did.startsWith(DID_KEY_PREFIX)) {
    throw new KeyError(`not a did:key: ${did}`, 'E_KID_UNRESOLVABLE');
  }
  let bytes: Uint8Array;
  try {
    bytes = base58btcDecode(did.slice(DID_KEY_PREFIX.length));
  } catch (e) {
    throw new KeyError(`did:key is not valid base58btc: ${(e as Error).message}`, 'E_KID_UNRESOLVABLE');
  }
  if (bytes.length < 3) {
    throw new KeyError(`did:key is too short: ${did}`, 'E_KID_UNRESOLVABLE');
  }
  return { multicodec: bytes.subarray(0, 2), publicKey: bytes.subarray(2) };
}

/** A public key resolved from a `did:key`, used only to verify. */
export class VerifyKey {
  readonly algorithm: SignatureAlgorithm;
  /** Ed25519: the 32 raw bytes. ES256: the 33-byte SEC1-compressed point. */
  readonly publicKey: Uint8Array;

  private constructor(algorithm: SignatureAlgorithm, publicKey: Uint8Array) {
    this.algorithm = algorithm;
    this.publicKey = publicKey;
  }

  static fromDidKey(did: string): VerifyKey {
    const { multicodec, publicKey } = decodeDidKey(did);
    if (bytesMatch(multicodec, MULTICODEC_ED25519_PUB)) {
      if (publicKey.length !== 32) {
        throw new KeyError('ed25519 did:key must carry 32 bytes', 'E_KID_UNRESOLVABLE');
      }
      return new VerifyKey('EdDSA', publicKey);
    }
    if (bytesMatch(multicodec, MULTICODEC_P256_PUB)) {
      if (publicKey.length !== 33) {
        throw new KeyError('p-256 did:key must carry a 33-byte compressed point', 'E_KID_UNRESOLVABLE');
      }
      // Reject a prefix that is well-formed but not a point on the curve, here
      // rather than at verify time, so "unresolvable key" and "bad signature"
      // stay distinguishable.
      try {
        p256.ProjectivePoint.fromHex(publicKey).assertValidity();
      } catch (e) {
        throw new KeyError(`p-256 did:key is not a valid point: ${(e as Error).message}`, 'E_KID_UNRESOLVABLE');
      }
      return new VerifyKey('ES256', publicKey);
    }
    throw new KeyError(
      `unsupported did:key type (multicodec 0x${[...multicodec].map((b) => b.toString(16).padStart(2, '0')).join('')})`,
      'E_KID_UNRESOLVABLE',
    );
  }

  static fromPublicBytes(algorithm: SignatureAlgorithm, publicKey: Uint8Array): VerifyKey {
    return new VerifyKey(algorithm, publicKey);
  }

  didKey(): string {
    return encodeDidKey(
      this.algorithm === 'EdDSA' ? MULTICODEC_ED25519_PUB : MULTICODEC_P256_PUB,
      this.publicKey,
    );
  }

  /**
   * True iff `signature` is valid for `data`. A malformed signature is a
   * failed verification, not an exception — every caller here is asking a
   * yes/no question about untrusted bytes.
   */
  verify(data: Uint8Array, signature: Uint8Array): boolean {
    try {
      if (this.algorithm === 'EdDSA') {
        return ed25519.verify(signature, data, this.publicKey);
      }
      if (signature.length !== 64) return false;
      // ES256 signatures travel as raw R||S (RFC 7518 §3.4). `lowS: false` is
      // required: OpenSSL does not normalise S, so half of the reference
      // implementation's signatures would otherwise be rejected as malleable.
      return p256.verify(signature, sha256(data), this.publicKey, { lowS: false });
    } catch {
      return false;
    }
  }
}

/** A private signing key. */
export class SigningKey {
  readonly algorithm: SignatureAlgorithm;
  /** Ed25519: the 32-byte seed. ES256: the 32-byte scalar. */
  readonly privateBytes: Uint8Array;

  private constructor(algorithm: SignatureAlgorithm, privateBytes: Uint8Array) {
    this.algorithm = algorithm;
    this.privateBytes = privateBytes;
  }

  static generate(algorithm: SignatureAlgorithm): SigningKey {
    const curve = algorithm === 'EdDSA' ? ed25519 : p256;
    return new SigningKey(algorithm, curve.utils.randomPrivateKey());
  }

  static fromPrivateBytes(algorithm: SignatureAlgorithm, privateBytes: Uint8Array): SigningKey {
    if (privateBytes.length !== 32) {
      throw new KeyError(`${algorithm} private key must be 32 bytes, got ${privateBytes.length}`);
    }
    return new SigningKey(algorithm, Uint8Array.from(privateBytes));
  }

  publicKey(): VerifyKey {
    const raw =
      this.algorithm === 'EdDSA'
        ? ed25519.getPublicKey(this.privateBytes)
        : p256.getPublicKey(this.privateBytes, true); // compressed, as did:key wants
    return VerifyKey.fromPublicBytes(this.algorithm, raw);
  }

  didKey(): string {
    return this.publicKey().didKey();
  }

  sign(data: Uint8Array): Uint8Array {
    if (this.algorithm === 'EdDSA') return ed25519.sign(data, this.privateBytes);
    return p256.sign(sha256(data), this.privateBytes).toCompactRawBytes();
  }

  // -- PKCS#8 interop -------------------------------------------------------

  toPkcs8Pem(): string {
    return pemEncode('PRIVATE KEY', this.#toPkcs8Der());
  }

  #toPkcs8Der(): Uint8Array {
    if (this.algorithm === 'EdDSA') {
      // RFC 8410 §7: the private key is an OCTET STRING wrapping the seed's own
      // OCTET STRING — the double wrap is not a mistake.
      return sequence(
        smallInteger(0),
        sequence(oid(OID_ED25519)),
        octetString(octetString(this.privateBytes)),
      );
    }
    const publicPoint = p256.getPublicKey(this.privateBytes, false); // uncompressed
    const ecPrivateKey = sequence(
      smallInteger(1),
      octetString(this.privateBytes),
      contextTag(1, bitString(publicPoint)),
    );
    return sequence(
      smallInteger(0),
      sequence(oid(OID_EC_PUBLIC_KEY), oid(OID_PRIME256V1)),
      octetString(ecPrivateKey),
    );
  }

  /** Reads an unencrypted PKCS#8 PEM — what `mcdf keygen` writes. */
  static fromPkcs8Pem(pem: string): SigningKey {
    return SigningKey.fromPkcs8Der(pemDecode(pem, 'PRIVATE KEY'));
  }

  static fromPkcs8Der(der: Uint8Array): SigningKey {
    const outer = new DerReader(der);
    const body = new DerReader(outer.expect(TAG_SEQUENCE, 'PrivateKeyInfo'));

    body.expect(TAG_INTEGER, 'version');
    const algorithmId = new DerReader(body.expect(TAG_SEQUENCE, 'AlgorithmIdentifier'));
    const algorithmOid = algorithmId.expect(TAG_OID, 'algorithm OID');
    const privateKeyInfo = body.expect(TAG_OCTET_STRING, 'privateKey');

    if (bytesMatch(algorithmOid, OID_ED25519)) {
      const seed = new DerReader(privateKeyInfo).expect(TAG_OCTET_STRING, 'CurvePrivateKey');
      return SigningKey.fromPrivateBytes('EdDSA', seed);
    }

    if (bytesMatch(algorithmOid, OID_EC_PUBLIC_KEY)) {
      const curve = algorithmId.done ? new Uint8Array() : algorithmId.expect(TAG_OID, 'curve OID');
      if (!bytesMatch(curve, OID_PRIME256V1)) {
        throw new KeyError('only the P-256 curve is supported for ES256');
      }
      const ec = new DerReader(new DerReader(privateKeyInfo).expect(TAG_SEQUENCE, 'ECPrivateKey'));
      ec.expect(TAG_INTEGER, 'ECPrivateKey version');
      const scalar = ec.expect(TAG_OCTET_STRING, 'ECPrivateKey privateKey');
      // RFC 5915 fixes the scalar's width at the curve's, but a producer that
      // strips leading zeros is common enough to accommodate.
      const padded = new Uint8Array(32);
      if (scalar.length > 32) throw new KeyError('P-256 private scalar is too long');
      padded.set(scalar, 32 - scalar.length);
      return SigningKey.fromPrivateBytes('ES256', padded);
    }

    throw new KeyError('unsupported private key algorithm (expected Ed25519 or EC P-256)');
  }
}
