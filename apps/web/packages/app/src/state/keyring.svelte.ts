// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// One reactive view of the key store, shared by every panel that needs keys.
//
// The first cut gave each panel its own `listKeys()` snapshot taken at mount.
// Panels inside a closed <details> still mount, so all three read the store
// before the user had generated anything — and a key created in Trust never
// appeared in Encryption or Audit until the page was reloaded. State that more
// than one component both reads and writes belongs in one place; this is it.

import {
  deleteKey,
  generateKey,
  importPem,
  isKeystoreAvailable,
  listKeys,
  type KeyAlgorithm,
  type StoredKey,
} from '../platform/keystore.ts';

export class Keyring {
  keys = $state<StoredKey[]>([]);
  /** Set when the store itself is unusable, not when an operation failed. */
  error = $state('');
  loaded = $state(false);

  readonly available = isKeystoreAvailable();

  signing = $derived(this.keys.filter((k) => k.purpose === 'signing'));
  recipients = $derived(this.keys.filter((k) => k.purpose === 'recipient'));

  constructor() {
    if (!this.available) {
      this.error = 'This browser has no IndexedDB, so keys cannot be kept between visits.';
      this.loaded = true;
      return;
    }
    void this.refresh();
  }

  async refresh(): Promise<void> {
    try {
      this.keys = await listKeys();
      this.error = '';
    } catch (e) {
      this.error = (e as Error).message;
    } finally {
      this.loaded = true;
    }
  }

  async generate(algorithm: KeyAlgorithm, label: string): Promise<StoredKey> {
    const key = await generateKey(algorithm, label);
    await this.refresh();
    return key;
  }

  async import(pem: string, label: string): Promise<StoredKey> {
    const key = await importPem(pem, label);
    await this.refresh();
    return key;
  }

  async remove(did: string): Promise<void> {
    await deleteKey(did);
    await this.refresh();
  }

  has(did: string): boolean {
    return this.keys.some((k) => k.did === did);
  }

  find(did: string): StoredKey | undefined {
    return this.keys.find((k) => k.did === did);
  }
}

export const keyring = new Keyring();
