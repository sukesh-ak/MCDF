// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { sha256 } from '@noble/hashes/sha256';

import { bytesToHex } from '../util/bytes.js';
import { DEFAULT_HASH_ALGORITHM } from '../model/types.js';

export class HashError extends Error {
  readonly code = 'E_ALGO_NOT_ALLOWED';

  constructor(message: string) {
    super(message);
    this.name = 'HashError';
  }
}

export function isAllowedHashAlgorithm(algorithm: string): boolean {
  return algorithm === DEFAULT_HASH_ALGORITHM;
}

/**
 * Hex digest of `data` under `algorithm`.
 *
 * Synchronous by design: manifest building touches every member, and a
 * synchronous digest keeps that a plain loop in workers, Node and the browser
 * alike (WebCrypto only offers an async `digest`).
 */
export function hashHex(algorithm: string, data: Uint8Array): string {
  if (!isAllowedHashAlgorithm(algorithm)) {
    throw new HashError(`hash algorithm not allowed: ${algorithm}`);
  }
  return bytesToHex(sha256(data));
}

export function sha256Hex(data: Uint8Array): string {
  return bytesToHex(sha256(data));
}
