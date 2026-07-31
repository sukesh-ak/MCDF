// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Byte helpers. Member payloads are always `Uint8Array` in this implementation:
// hashing, TAR framing and encryption all operate on the bytes on disk, never on
// a decoded string. Text is decoded only at the edges.

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: false });

export function utf8Encode(text: string): Uint8Array {
  return encoder.encode(text);
}

export function utf8Decode(bytes: Uint8Array): string {
  return decoder.decode(bytes);
}

/**
 * Orders two strings by their UTF-8 byte sequence.
 *
 * The reference implementation keeps members in a `std::map<std::string, ...>`,
 * which compares raw bytes. JavaScript's default `<` compares UTF-16 code
 * units, and the two disagree above U+FFFF (surrogates sort below U+E000 in
 * UTF-16 but above it in UTF-8), so TAR member ordering must not use `<`.
 */
export function compareUtf8(a: string, b: string): number {
  const ab = utf8Encode(a);
  const bb = utf8Encode(b);
  const n = Math.min(ab.length, bb.length);
  for (let i = 0; i < n; i++) {
    const x = ab[i] as number;
    const y = bb[i] as number;
    if (x !== y) return x - y;
  }
  return ab.length - bb.length;
}

export function concatBytes(chunks: Uint8Array[]): Uint8Array {
  let total = 0;
  for (const c of chunks) total += c.length;
  const out = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    out.set(c, at);
    at += c.length;
  }
  return out;
}

export function bytesEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

const HEX = '0123456789abcdef';

export function bytesToHex(bytes: Uint8Array): string {
  let out = '';
  for (const b of bytes) {
    out += HEX[(b >> 4) & 0x0f];
    out += HEX[b & 0x0f];
  }
  return out;
}
