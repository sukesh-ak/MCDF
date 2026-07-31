// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The key store: IndexedDB, origin-scoped, never transmitted.
//
// WHAT THIS IS AND IS NOT. Keys are stored as raw private bytes. IndexedDB is
// isolated per origin, so no other site can read them — but any script running
// on *this* origin can, and so can anyone with the device unlocked. That is the
// honest security level of a keyring in a web page, and the UI says so rather
// than implying a hardware token.
//
// The alternative — non-extractable WebCrypto `CryptoKey` objects, which
// IndexedDB can store and JavaScript cannot read back — is genuinely stronger
// and is not available here: it would only cover ES256, because Ed25519 support
// in WebCrypto is still uneven (the same reason mcdf-ts verifies with
// @noble/curves). Supporting one algorithm properly and the other badly is
// worse than one predictable model, so both are handled the same way.
//
// For keys that matter, `mcdf-ts keygen` on a machine you control and a PEM you
// keep offline is the right answer; this store is for working keys.

import { EncPrivateKey, SigningKey, type SignatureAlgorithm } from 'mcdf-ts';

const DB_NAME = 'mcdf-keystore';
const DB_VERSION = 1;
const STORE = 'keys';

export type KeyPurpose = 'signing' | 'recipient';

/** `EdDSA` and `ES256` sign; `X25519` receives encrypted documents. */
export type KeyAlgorithm = SignatureAlgorithm | 'X25519';

export interface StoredKey {
  /** The `did:key` — the identity the key *is*, so a natural primary key. */
  did: string;
  label: string;
  purpose: KeyPurpose;
  algorithm: KeyAlgorithm;
  privateBytes: Uint8Array;
  createdAt: string;
}

export function isSigningKey(key: StoredKey): boolean {
  return key.purpose === 'signing';
}

function open(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE, { keyPath: 'did' });
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('cannot open the key store'));
  });
}

function run<T>(mode: IDBTransactionMode, body: (store: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  return open().then(
    (db) =>
      new Promise<T>((resolve, reject) => {
        const tx = db.transaction(STORE, mode);
        const request = body(tx.objectStore(STORE));
        request.onsuccess = () => resolve(request.result);
        request.onerror = () => reject(request.error ?? new Error('key store request failed'));
        tx.oncomplete = () => db.close();
      }),
  );
}

/** True when IndexedDB is usable — private-browsing modes sometimes block it. */
export function isKeystoreAvailable(): boolean {
  return typeof indexedDB !== 'undefined';
}

export async function listKeys(): Promise<StoredKey[]> {
  const keys = (await run<StoredKey[]>('readonly', (s) => s.getAll())) ?? [];
  return keys.sort((a, b) => a.createdAt.localeCompare(b.createdAt));
}

export async function putKey(key: StoredKey): Promise<void> {
  await run('readwrite', (s) => s.put(key));
}

export async function deleteKey(did: string): Promise<void> {
  await run('readwrite', (s) => s.delete(did));
}

/** Generates a key and stores it. Returns the stored record. */
export async function generateKey(
  algorithm: KeyAlgorithm,
  label: string,
): Promise<StoredKey> {
  const createdAt = new Date().toISOString();
  if (algorithm === 'X25519') {
    const key = EncPrivateKey.generate();
    const stored: StoredKey = {
      did: key.didKey(),
      label,
      purpose: 'recipient',
      algorithm,
      privateBytes: key.raw,
      createdAt,
    };
    await putKey(stored);
    return stored;
  }

  const key = SigningKey.generate(algorithm);
  const stored: StoredKey = {
    did: key.didKey(),
    label,
    purpose: 'signing',
    algorithm,
    privateBytes: key.privateBytes,
    createdAt,
  };
  await putKey(stored);
  return stored;
}

/** Imports a PKCS#8 PEM — the same file `mcdf keygen` and `mcdf-ts keygen` write. */
export async function importPem(pem: string, label: string): Promise<StoredKey> {
  const createdAt = new Date().toISOString();

  // Try signing first, then recipient: the two are distinguished by their
  // algorithm OID, so exactly one parse can succeed for a well-formed key.
  try {
    const key = SigningKey.fromPkcs8Pem(pem);
    const stored: StoredKey = {
      did: key.didKey(),
      label,
      purpose: 'signing',
      algorithm: key.algorithm,
      privateBytes: key.privateBytes,
      createdAt,
    };
    await putKey(stored);
    return stored;
  } catch (signingError) {
    try {
      const key = EncPrivateKey.fromPkcs8Pem(pem);
      const stored: StoredKey = {
        did: key.didKey(),
        label,
        purpose: 'recipient',
        algorithm: 'X25519',
        privateBytes: key.raw,
        createdAt,
      };
      await putKey(stored);
      return stored;
    } catch {
      // Report the signing-key failure: it is the likelier intent, and its
      // message is the more specific of the two.
      throw signingError;
    }
  }
}

export function toSigningKey(stored: StoredKey): SigningKey {
  if (stored.purpose !== 'signing') {
    throw new Error(`${stored.label} is a recipient key and cannot sign`);
  }
  return SigningKey.fromPrivateBytes(stored.algorithm as SignatureAlgorithm, stored.privateBytes);
}

export function toRecipientKey(stored: StoredKey): EncPrivateKey {
  if (stored.purpose !== 'recipient') {
    throw new Error(`${stored.label} is a signing key and cannot decrypt`);
  }
  return EncPrivateKey.fromRaw(stored.privateBytes);
}

export function exportPem(stored: StoredKey): string {
  return stored.purpose === 'signing'
    ? toSigningKey(stored).toPkcs8Pem()
    : toRecipientKey(stored).toPkcs8Pem();
}

/** Shortens a `did:key` for display without losing its distinguishing tail. */
export function shortDid(did: string): string {
  return did.length <= 28 ? did : `${did.slice(0, 18)}…${did.slice(-6)}`;
}
