// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Just enough DER and PEM to read and write PKCS#8 private keys.
//
// Why at all: `did:key` covers every *public* key MCDF needs, so verification
// never comes near this file. Private keys are the interop seam — the reference
// CLI reads and writes PKCS#8 PEM (`mcdf keygen`, `mcdf sign --key`), and a key
// pair that only one implementation can load would quietly make "independent
// implementation" mean "separate island". This is the smallest amount of ASN.1
// that lets the same `key.pem` sign in the browser and verify at the terminal.
//
// Deliberately narrow: two key shapes, no BER, no indefinite lengths, no
// certificates. Anything else is a parse error rather than a best guess.

export class DerError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'DerError';
  }
}

export const TAG_INTEGER = 0x02;
export const TAG_BIT_STRING = 0x03;
export const TAG_OCTET_STRING = 0x04;
export const TAG_OID = 0x06;
export const TAG_SEQUENCE = 0x30;

/** Object identifiers, pre-encoded as their DER content bytes. */
export const OID_ED25519 = Uint8Array.of(0x2b, 0x65, 0x70); // 1.3.101.112
export const OID_X25519 = Uint8Array.of(0x2b, 0x65, 0x6e); // 1.3.101.110
export const OID_EC_PUBLIC_KEY = Uint8Array.of(0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01);
export const OID_PRIME256V1 = Uint8Array.of(0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07);

function encodeLength(n: number): number[] {
  if (n < 0x80) return [n];
  const bytes: number[] = [];
  for (let v = n; v > 0; v = Math.floor(v / 256)) bytes.unshift(v % 256);
  return [0x80 | bytes.length, ...bytes];
}

/** One DER type-length-value. */
export function tlv(tag: number, content: Uint8Array): Uint8Array {
  const header = [tag, ...encodeLength(content.length)];
  const out = new Uint8Array(header.length + content.length);
  out.set(header, 0);
  out.set(content, header.length);
  return out;
}

export function sequence(...parts: Uint8Array[]): Uint8Array {
  let total = 0;
  for (const p of parts) total += p.length;
  const content = new Uint8Array(total);
  let at = 0;
  for (const p of parts) {
    content.set(p, at);
    at += p.length;
  }
  return tlv(TAG_SEQUENCE, content);
}

/** A small non-negative INTEGER — versions only, so one content byte suffices. */
export function smallInteger(value: number): Uint8Array {
  if (value < 0 || value > 0x7f) throw new DerError(`integer out of range: ${value}`);
  return tlv(TAG_INTEGER, Uint8Array.of(value));
}

export function octetString(content: Uint8Array): Uint8Array {
  return tlv(TAG_OCTET_STRING, content);
}

export function oid(content: Uint8Array): Uint8Array {
  return tlv(TAG_OID, content);
}

/** A BIT STRING with no unused trailing bits — the only kind keys use. */
export function bitString(content: Uint8Array): Uint8Array {
  const body = new Uint8Array(content.length + 1);
  body[0] = 0; // unused-bit count
  body.set(content, 1);
  return tlv(TAG_BIT_STRING, body);
}

/** Context-specific constructed tag `[n]`. */
export function contextTag(n: number, content: Uint8Array): Uint8Array {
  return tlv(0xa0 | n, content);
}

export interface DerNode {
  tag: number;
  content: Uint8Array;
}

/** A forward-only cursor over a DER sequence's contents. */
export class DerReader {
  #bytes: Uint8Array;
  #at = 0;

  constructor(bytes: Uint8Array) {
    this.#bytes = bytes;
  }

  get done(): boolean {
    return this.#at >= this.#bytes.length;
  }

  /** Reads the next TLV, or throws when the input is truncated or malformed. */
  next(): DerNode {
    if (this.#at + 2 > this.#bytes.length) throw new DerError('truncated DER');
    const tag = this.#bytes[this.#at++] as number;
    let length = this.#bytes[this.#at++] as number;
    if (length & 0x80) {
      const count = length & 0x7f;
      // Indefinite length (0x80) is BER, not DER, and a 5+ byte length would be
      // a key larger than anything real; both mean "not a key we should try".
      if (count === 0 || count > 4) throw new DerError('unsupported DER length');
      if (this.#at + count > this.#bytes.length) throw new DerError('truncated DER length');
      length = 0;
      for (let i = 0; i < count; i++) length = length * 256 + (this.#bytes[this.#at++] as number);
    }
    if (this.#at + length > this.#bytes.length) throw new DerError('truncated DER value');
    const content = this.#bytes.subarray(this.#at, this.#at + length);
    this.#at += length;
    return { tag, content };
  }

  /** Reads the next TLV and asserts its tag. */
  expect(tag: number, what: string): Uint8Array {
    const node = this.next();
    if (node.tag !== tag) {
      throw new DerError(`expected ${what} (tag 0x${tag.toString(16)}), got 0x${node.tag.toString(16)}`);
    }
    return node.content;
  }
}

export function bytesMatch(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

// ---------------------------------------------------------------------------
// PEM
// ---------------------------------------------------------------------------

const B64_STD = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const B64_STD_VALUES = new Int8Array(128).fill(-1);
for (let i = 0; i < B64_STD.length; i++) B64_STD_VALUES[B64_STD.charCodeAt(i)] = i;

function base64Encode(data: Uint8Array): string {
  let out = '';
  let i = 0;
  for (; i + 3 <= data.length; i += 3) {
    const n = ((data[i] as number) << 16) | ((data[i + 1] as number) << 8) | (data[i + 2] as number);
    out += B64_STD[(n >> 18) & 63];
    out += B64_STD[(n >> 12) & 63];
    out += B64_STD[(n >> 6) & 63];
    out += B64_STD[n & 63];
  }
  const rest = data.length - i;
  if (rest === 1) {
    const n = (data[i] as number) << 16;
    out += `${B64_STD[(n >> 18) & 63]}${B64_STD[(n >> 12) & 63]}==`;
  } else if (rest === 2) {
    const n = ((data[i] as number) << 16) | ((data[i + 1] as number) << 8);
    out += `${B64_STD[(n >> 18) & 63]}${B64_STD[(n >> 12) & 63]}${B64_STD[(n >> 6) & 63]}=`;
  }
  return out;
}

function base64Decode(text: string): Uint8Array {
  const out: number[] = [];
  let buffer = 0;
  let bits = 0;
  for (const ch of text) {
    if (ch === '=' || ch === '\n' || ch === '\r' || ch === ' ' || ch === '\t') continue;
    const code = ch.charCodeAt(0);
    const value = code < 128 ? (B64_STD_VALUES[code] as number) : -1;
    if (value < 0) throw new DerError(`invalid base64 character in PEM: ${ch}`);
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push((buffer >> bits) & 0xff);
    }
  }
  return Uint8Array.from(out);
}

/** Wraps DER in a PEM block with 64-character lines, as OpenSSL writes them. */
export function pemEncode(label: string, der: Uint8Array): string {
  const body = base64Encode(der).replace(/(.{64})/g, '$1\n');
  const trailing = body.endsWith('\n') ? '' : '\n';
  return `-----BEGIN ${label}-----\n${body}${trailing}-----END ${label}-----\n`;
}

/** Extracts the DER from a PEM block, checking the label. */
export function pemDecode(pem: string, label: string): Uint8Array {
  const begin = `-----BEGIN ${label}-----`;
  const end = `-----END ${label}-----`;
  const from = pem.indexOf(begin);
  const to = pem.indexOf(end);
  if (from === -1 || to === -1 || to < from) {
    throw new DerError(`not a ${label} PEM block`);
  }
  return base64Decode(pem.slice(from + begin.length, to));
}
