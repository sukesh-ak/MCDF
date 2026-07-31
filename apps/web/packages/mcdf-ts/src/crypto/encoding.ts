// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// base64url (RFC 4648 §5, unpadded) and base58btc — the two alphabets MCDF
// needs: JWS segments are base64url, `did:key` is base58btc after the `z`
// multibase prefix.
//
// Written in-house rather than taken from a package. Both are
// tiny, both are on the byte-parity path with `src/crypto/encoding.cpp`, and a
// dependency whose output must match another implementation character for
// character is a dependency worth not having.

export class EncodingError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EncodingError';
  }
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
const B58 = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';

/** Reverse lookup tables, built once — the decoders run per signature segment. */
const B64_VALUES = new Int8Array(128).fill(-1);
for (let i = 0; i < B64.length; i++) B64_VALUES[B64.charCodeAt(i)] = i;

const B58_VALUES = new Int8Array(128).fill(-1);
for (let i = 0; i < B58.length; i++) B58_VALUES[B58.charCodeAt(i)] = i;

export function base64urlEncode(data: Uint8Array): string {
  let out = '';
  let i = 0;
  for (; i + 3 <= data.length; i += 3) {
    const n = ((data[i] as number) << 16) | ((data[i + 1] as number) << 8) | (data[i + 2] as number);
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];
    out += B64[n & 63];
  }
  const rest = data.length - i;
  if (rest === 1) {
    const n = (data[i] as number) << 16;
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
  } else if (rest === 2) {
    const n = ((data[i] as number) << 16) | ((data[i + 1] as number) << 8);
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];
  }
  return out;
}

/**
 * Decodes unpadded base64url. `=` padding is tolerated on input (some producers
 * emit it even where JWS forbids it) but never produced.
 */
export function base64urlDecode(text: string): Uint8Array {
  const out: number[] = [];
  let buffer = 0;
  let bits = 0;
  for (const ch of text) {
    if (ch === '=') continue;
    const code = ch.charCodeAt(0);
    const value = code < 128 ? (B64_VALUES[code] as number) : -1;
    if (value < 0) throw new EncodingError(`invalid base64url character: ${ch}`);
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push((buffer >> bits) & 0xff);
    }
  }
  return Uint8Array.from(out);
}

export function base58btcEncode(data: Uint8Array): string {
  let zeros = 0;
  while (zeros < data.length && data[zeros] === 0) zeros++;

  const digits: number[] = []; // little-endian base-58
  for (let i = zeros; i < data.length; i++) {
    let carry = data[i] as number;
    for (let d = 0; d < digits.length; d++) {
      const x = (digits[d] as number) * 256 + carry;
      digits[d] = x % 58;
      carry = (x / 58) | 0;
    }
    while (carry > 0) {
      digits.push(carry % 58);
      carry = (carry / 58) | 0;
    }
  }

  let out = '1'.repeat(zeros);
  for (let i = digits.length - 1; i >= 0; i--) out += B58[digits[i] as number];
  return out;
}

export function base58btcDecode(text: string): Uint8Array {
  let zeros = 0;
  while (zeros < text.length && text[zeros] === '1') zeros++;

  const bytes: number[] = []; // little-endian base-256
  for (let i = zeros; i < text.length; i++) {
    const code = text.charCodeAt(i);
    const value = code < 128 ? (B58_VALUES[code] as number) : -1;
    if (value < 0) throw new EncodingError(`invalid base58 character: ${text[i]}`);
    let carry = value;
    for (let b = 0; b < bytes.length; b++) {
      const x = (bytes[b] as number) * 58 + carry;
      bytes[b] = x % 256;
      carry = (x / 256) | 0;
    }
    while (carry > 0) {
      bytes.push(carry % 256);
      carry = (carry / 256) | 0;
    }
  }

  const out = new Uint8Array(zeros + bytes.length);
  for (let i = 0; i < bytes.length; i++) out[zeros + i] = bytes[bytes.length - 1 - i] as number;
  return out;
}
