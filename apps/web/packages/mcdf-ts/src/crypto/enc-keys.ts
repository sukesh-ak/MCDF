// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// HPKE recipient keys (X25519), ported from `src/crypto/enc_keys.cpp`.
//
// The suite is fixed by the spec: DHKEM(X25519, HKDF-SHA256) / HKDF-SHA256 /
// AES-256-GCM, RFC 9180 base mode. @hpke/core implements the protocol;
// @noble/curves derives a public key from a stored private one, which is plain
// scalar multiplication and keeps key handling synchronous — only the actual
// seal and open have to be awaited.

import { Aes256Gcm, CipherSuite, DhkemX25519HkdfSha256, HkdfSha256 } from '@hpke/core';
import { x25519 } from '@noble/curves/ed25519';

import {
  DerReader,
  OID_X25519,
  TAG_INTEGER,
  TAG_OCTET_STRING,
  TAG_OID,
  TAG_SEQUENCE,
  bytesMatch,
  octetString,
  oid,
  pemDecode,
  pemEncode,
  sequence,
  smallInteger,
} from './der.js';
import { base58btcDecode, base58btcEncode } from './encoding.js';
import { KeyError } from './keys.js';

/** The HPKE `info` string binding a wrapped key to its purpose in MCDF. */
export const HPKE_INFO = 'mcdf/hpke/cek/v1';

/** Multicodec prefix (varint 0xec01) for an X25519 public key. */
const MULTICODEC_X25519_PUB = Uint8Array.of(0xec, 0x01);

const DID_KEY_PREFIX = 'did:key:z';

/**
 * One suite instance for the process. `CipherSuite` is stateless — contexts
 * carry the per-message state — so sharing it avoids re-deriving the same
 * primitives on every recipient.
 */
const suite = new CipherSuite({
  kem: new DhkemX25519HkdfSha256(),
  kdf: new HkdfSha256(),
  aead: new Aes256Gcm(),
});

function toBytes(buffer: ArrayBuffer): Uint8Array {
  return new Uint8Array(buffer);
}

/** Copies into a standalone ArrayBuffer — @hpke/core will not take a view. */
function toArrayBuffer(bytes: Uint8Array): ArrayBuffer {
  return bytes.slice().buffer as ArrayBuffer;
}

/** An X25519 public key: a recipient a document can be encrypted to. */
export class EncPublicKey {
  readonly raw: Uint8Array;

  private constructor(raw: Uint8Array) {
    this.raw = raw;
  }

  static fromRaw(raw: Uint8Array): EncPublicKey {
    if (raw.length !== 32) {
      throw new KeyError(`x25519 public key must be 32 bytes, got ${raw.length}`);
    }
    return new EncPublicKey(Uint8Array.from(raw));
  }

  static fromDidKey(did: string): EncPublicKey {
    if (!did.startsWith(DID_KEY_PREFIX)) {
      throw new KeyError(`not a did:key: ${did}`, 'E_KID_UNRESOLVABLE');
    }
    let bytes: Uint8Array;
    try {
      bytes = base58btcDecode(did.slice(DID_KEY_PREFIX.length));
    } catch (e) {
      throw new KeyError(
        `did:key is not valid base58btc: ${(e as Error).message}`,
        'E_KID_UNRESOLVABLE',
      );
    }
    if (
      bytes.length !== MULTICODEC_X25519_PUB.length + 32 ||
      bytes[0] !== MULTICODEC_X25519_PUB[0] ||
      bytes[1] !== MULTICODEC_X25519_PUB[1]
    ) {
      throw new KeyError(`did:key is not an x25519 key: ${did}`, 'E_KID_UNRESOLVABLE');
    }
    return new EncPublicKey(bytes.slice(MULTICODEC_X25519_PUB.length));
  }

  didKey(): string {
    const bytes = new Uint8Array(MULTICODEC_X25519_PUB.length + this.raw.length);
    bytes.set(MULTICODEC_X25519_PUB, 0);
    bytes.set(this.raw, MULTICODEC_X25519_PUB.length);
    return DID_KEY_PREFIX + base58btcEncode(bytes);
  }
}

export interface HpkeSealed {
  /** The encapsulated key. */
  enc: Uint8Array;
  /** The sealed plaintext. */
  ct: Uint8Array;
}

/** Wraps `plaintext` (the CEK) to a recipient under RFC 9180 base mode. */
export async function hpkeSeal(
  recipient: EncPublicKey,
  info: string,
  plaintext: Uint8Array,
): Promise<HpkeSealed> {
  const recipientPublicKey = await suite.kem.importKey('raw', toArrayBuffer(recipient.raw), true);
  const sender = await suite.createSenderContext({
    recipientPublicKey,
    info: new TextEncoder().encode(info),
  });
  const ct = await sender.seal(toArrayBuffer(plaintext));
  return { enc: toBytes(sender.enc), ct: toBytes(ct) };
}

/** An X25519 private key: the half that can open a wrapped CEK. */
export class EncPrivateKey {
  readonly raw: Uint8Array;

  private constructor(raw: Uint8Array) {
    this.raw = raw;
  }

  static generate(): EncPrivateKey {
    return new EncPrivateKey(x25519.utils.randomPrivateKey());
  }

  static fromRaw(raw: Uint8Array): EncPrivateKey {
    if (raw.length !== 32) {
      throw new KeyError(`x25519 private key must be 32 bytes, got ${raw.length}`);
    }
    return new EncPrivateKey(Uint8Array.from(raw));
  }

  publicKey(): EncPublicKey {
    return EncPublicKey.fromRaw(x25519.getPublicKey(this.raw));
  }

  didKey(): string {
    return this.publicKey().didKey();
  }

  /** PKCS#8 PEM, the form `mcdf keygen --type x25519` writes (RFC 8410 §7). */
  toPkcs8Pem(): string {
    return pemEncode(
      'PRIVATE KEY',
      sequence(
        smallInteger(0),
        sequence(oid(OID_X25519)),
        octetString(octetString(this.raw)),
      ),
    );
  }

  static fromPkcs8Pem(pem: string): EncPrivateKey {
    const body = new DerReader(
      new DerReader(pemDecode(pem, 'PRIVATE KEY')).expect(TAG_SEQUENCE, 'PrivateKeyInfo'),
    );
    body.expect(TAG_INTEGER, 'version');
    const algorithmOid = new DerReader(
      body.expect(TAG_SEQUENCE, 'AlgorithmIdentifier'),
    ).expect(TAG_OID, 'algorithm OID');
    if (!bytesMatch(algorithmOid, OID_X25519)) {
      throw new KeyError('not an X25519 private key (recipients use X25519, signers do not)');
    }
    const inner = body.expect(TAG_OCTET_STRING, 'privateKey');
    return EncPrivateKey.fromRaw(new DerReader(inner).expect(TAG_OCTET_STRING, 'CurvePrivateKey'));
  }

  /** Unwraps a CEK sealed to this key. Throws when this key is not the recipient. */
  async hpkeOpen(enc: Uint8Array, ct: Uint8Array, info: string): Promise<Uint8Array> {
    const recipientKey = await suite.kem.importKey('raw', toArrayBuffer(this.raw), false);
    const recipient = await suite.createRecipientContext({
      recipientKey,
      enc: toArrayBuffer(enc),
      info: new TextEncoder().encode(info),
    });
    return toBytes(await recipient.open(toArrayBuffer(ct)));
  }
}
