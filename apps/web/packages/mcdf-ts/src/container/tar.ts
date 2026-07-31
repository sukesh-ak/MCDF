// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Deterministic USTAR reader/writer — the `.mcdf` single-file form.
//
// Byte-parity with the reference C++ writer is a hard requirement (the same
// document must hash identically whichever implementation packed it), so the
// normalisation rules here mirror `src/container/tar.cpp` exactly: members
// ordered by path, mtime/uid/gid zeroed, mode 0644, empty owner names, two zero
// blocks at the end and no record padding beyond them.

import { compareUtf8, concatBytes, utf8Encode } from '../util/bytes.js';

const BLOCK = 512;

export class TarError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'TarError';
  }
}

export interface TarMember {
  path: string;
  data: Uint8Array;
}

/** Writes `value` as right-justified octal in `width` bytes, last byte NUL. */
function putOctal(block: Uint8Array, offset: number, width: number, value: number): void {
  block[offset + width - 1] = 0;
  let v = value;
  for (let i = width - 1; i > 0; i--) {
    block[offset + i - 1] = 0x30 + (v & 7);
    v = Math.floor(v / 8);
  }
}

function putAscii(block: Uint8Array, offset: number, text: string): void {
  const bytes = utf8Encode(text);
  block.set(bytes, offset);
}

/** Splits a path into ustar name (<=100) and prefix (<=155) at a '/' boundary. */
function splitPath(path: string): { name: string; prefix: string } | null {
  if (utf8Encode(path).length <= 100) return { name: path, prefix: '' };
  if (utf8Encode(path).length > 255) return null;
  for (let i = 0; i < path.length; i++) {
    if (path[i] !== '/') continue;
    const head = path.slice(0, i);
    const tail = path.slice(i + 1);
    if (tail.length > 0 && utf8Encode(head).length <= 155 && utf8Encode(tail).length <= 100) {
      return { name: tail, prefix: head };
    }
  }
  return null;
}

function writeHeader(path: string, size: number): Uint8Array {
  const block = new Uint8Array(BLOCK);
  const split = splitPath(path);
  if (split === null) throw new TarError(`path too long for tar: ${path}`);

  putAscii(block, 0, split.name);
  putOctal(block, 100, 8, 0o644); // mode
  putOctal(block, 108, 8, 0); // uid
  putOctal(block, 116, 8, 0); // gid
  putOctal(block, 124, 12, size);
  putOctal(block, 136, 12, 0); // mtime (fixed -> deterministic)
  block[156] = 0x30; // typeflag '0': regular file
  putAscii(block, 257, 'ustar'); // magic "ustar\0"
  block[263] = 0x30;
  block[264] = 0x30; // version "00"
  if (split.prefix !== '') putAscii(block, 345, split.prefix);

  // Checksum: sum every byte with the checksum field treated as spaces.
  block.fill(0x20, 148, 156);
  let sum = 0;
  for (const b of block) sum += b;
  const octal = (sum & 0o777777).toString(8).padStart(6, '0');
  putAscii(block, 148, octal);
  block[154] = 0; // NUL
  block[155] = 0x20; // space
  return block;
}

/**
 * Serializes members into a deterministic USTAR archive.
 *
 * Members are emitted in UTF-8 byte order of their paths regardless of the
 * order they appear in `members`, so identical content always yields identical
 * bytes.
 */
export function tarWrite(members: Iterable<TarMember>): Uint8Array {
  const sorted = [...members].sort((a, b) => compareUtf8(a.path, b.path));
  const chunks: Uint8Array[] = [];
  for (const { path, data } of sorted) {
    chunks.push(writeHeader(path, data.length));
    chunks.push(data);
    const rem = data.length % BLOCK;
    if (rem !== 0) chunks.push(new Uint8Array(BLOCK - rem));
  }
  chunks.push(new Uint8Array(2 * BLOCK)); // two zero blocks mark end-of-archive
  return concatBytes(chunks);
}

function readField(block: Uint8Array, offset: number, size: number): string {
  let len = 0;
  while (len < size && block[offset + len] !== 0) len++;
  // ustar header fields are ASCII by definition; decode as UTF-8 for paths that
  // stretch that in practice.
  return new TextDecoder('utf-8').decode(block.subarray(offset, offset + len));
}

function readOctal(block: Uint8Array, offset: number, size: number): number {
  let value = 0;
  let i = 0;
  while (i < size && (block[offset + i] === 0x20 || block[offset + i] === 0)) i++;
  for (; i < size; i++) {
    const c = block[offset + i] as number;
    if (c < 0x30 || c > 0x37) break;
    value = value * 8 + (c - 0x30);
  }
  return value;
}

function isZeroBlock(block: Uint8Array): boolean {
  for (const b of block) if (b !== 0) return false;
  return true;
}

/** Parses a USTAR archive into members, verifying every header checksum. */
export function tarRead(archive: Uint8Array): TarMember[] {
  const out: TarMember[] = [];
  let pos = 0;
  while (pos + BLOCK <= archive.length) {
    const block = archive.subarray(pos, pos + BLOCK);
    if (isZeroBlock(block)) break;

    const stored = readOctal(block, 148, 8);
    let sum = 0;
    for (let i = 0; i < BLOCK; i++) {
      sum += i >= 148 && i < 156 ? 0x20 : (block[i] as number);
    }
    if (sum !== stored) throw new TarError('bad tar checksum');

    const name = readField(block, 0, 100);
    const prefix = readField(block, 345, 155);
    const full = prefix === '' ? name : `${prefix}/${name}`;
    const size = readOctal(block, 124, 12);
    const type = block[156];
    pos += BLOCK;

    if (type === 0x30 || type === 0) {
      if (pos + size > archive.length) throw new TarError('truncated tar data');
      out.push({ path: full, data: archive.slice(pos, pos + size) });
    }
    pos += Math.ceil(size / BLOCK) * BLOCK;
  }
  return out;
}
