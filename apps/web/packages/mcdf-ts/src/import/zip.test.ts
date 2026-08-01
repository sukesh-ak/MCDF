// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { ZipArchive, ZipError } from './zip.js';

// -- a real ZIP writer, so the fixtures are archives and not mock objects ----

function crc32(data: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

async function deflateRaw(data: Uint8Array): Promise<Uint8Array> {
  const stream = new ReadableStream<Uint8Array>({
    start(controller) {
      controller.enqueue(data);
      controller.close();
    },
  }).pipeThrough(new CompressionStream('deflate-raw')) as ReadableStream<Uint8Array>;
  const chunks: Uint8Array[] = [];
  let size = 0;
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    size += value.byteLength;
  }
  const out = new Uint8Array(size);
  let at = 0;
  for (const chunk of chunks) {
    out.set(chunk, at);
    at += chunk.byteLength;
  }
  return out;
}

export interface ZipInput {
  name: string;
  data: string | Uint8Array;
  /** Store rather than deflate. EPUB's `mimetype` entry must be stored. */
  stored?: boolean;
}

/** Builds a genuine ZIP archive: local headers, central directory, EOCD. */
export async function makeZip(inputs: ZipInput[], comment = ''): Promise<Uint8Array> {
  const encoder = new TextEncoder();
  const locals: Uint8Array[] = [];
  const centrals: Uint8Array[] = [];
  let offset = 0;

  for (const input of inputs) {
    const raw = typeof input.data === 'string' ? encoder.encode(input.data) : input.data;
    const stored = input.stored === true;
    const body = stored ? raw : await deflateRaw(raw);
    const name = encoder.encode(input.name);
    const sum = crc32(raw);

    const local = new Uint8Array(30 + name.length + body.length);
    const lv = new DataView(local.buffer);
    lv.setUint32(0, 0x04034b50, true);
    lv.setUint16(4, 20, true);
    lv.setUint16(8, stored ? 0 : 8, true);
    lv.setUint32(14, sum, true);
    lv.setUint32(18, body.length, true);
    lv.setUint32(22, raw.length, true);
    lv.setUint16(26, name.length, true);
    local.set(name, 30);
    local.set(body, 30 + name.length);
    locals.push(local);

    const central = new Uint8Array(46 + name.length);
    const cv = new DataView(central.buffer);
    cv.setUint32(0, 0x02014b50, true);
    cv.setUint16(4, 20, true);
    cv.setUint16(6, 20, true);
    cv.setUint16(10, stored ? 0 : 8, true);
    cv.setUint32(16, sum, true);
    cv.setUint32(20, body.length, true);
    cv.setUint32(24, raw.length, true);
    cv.setUint16(28, name.length, true);
    cv.setUint32(42, offset, true);
    central.set(name, 46);
    centrals.push(central);

    offset += local.length;
  }

  const centralSize = centrals.reduce((n, c) => n + c.length, 0);
  const commentBytes = encoder.encode(comment);
  const eocd = new Uint8Array(22 + commentBytes.length);
  const ev = new DataView(eocd.buffer);
  ev.setUint32(0, 0x06054b50, true);
  ev.setUint16(8, inputs.length, true);
  ev.setUint16(10, inputs.length, true);
  ev.setUint32(12, centralSize, true);
  ev.setUint32(16, offset, true);
  ev.setUint16(20, commentBytes.length, true);
  eocd.set(commentBytes, 22);

  const total =
    locals.reduce((n, l) => n + l.length, 0) + centralSize + eocd.length;
  const out = new Uint8Array(total);
  let at = 0;
  for (const part of [...locals, ...centrals, eocd]) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}

/**
 * Offset of the first central-directory record, read the way the reader reads
 * it. Tests that want to lie to the reader have to lie in the central
 * directory: that is the copy it trusts, and patching the local header instead
 * changes nothing (which is how two of these tests were wrong first).
 */
function centralOffset(bytes: Uint8Array): number {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let at = bytes.length - 22; at >= 0; at--) {
    if (view.getUint32(at, true) !== 0x06054b50) continue;
    if (at + 22 + view.getUint16(at + 20, true) === bytes.length) {
      return view.getUint32(at + 16, true);
    }
  }
  throw new Error('no EOCD in fixture');
}

// -- tests -------------------------------------------------------------------

describe('ZipArchive', () => {
  it('reads stored and deflated members', async () => {
    const long = 'the quick brown fox '.repeat(200);
    const zip = ZipArchive.open(
      await makeZip([
        { name: 'mimetype', data: 'application/epub+zip', stored: true },
        { name: 'long.txt', data: long },
      ]),
    );

    expect(zip.list()).toEqual(['mimetype', 'long.txt']);
    expect(await zip.readText('mimetype')).toBe('application/epub+zip');
    expect(await zip.readText('long.txt')).toBe(long);
  });

  it('returns null for a member that is not there', async () => {
    const zip = ZipArchive.open(await makeZip([{ name: 'a.txt', data: 'a' }]));
    expect(await zip.read('missing.txt')).toBeNull();
  });

  it('finds the EOCD past a comment that contains its signature', async () => {
    // The signature inside the comment is the trap: scanning backwards for the
    // first match without checking that the declared comment length reaches the
    // end of the file lands on the decoy.
    const decoy = 'PK padding padding';
    const zip = ZipArchive.open(await makeZip([{ name: 'a.txt', data: 'hello' }], decoy));
    expect(await zip.readText('a.txt')).toBe('hello');
  });

  it('keeps the first of duplicate entry names and reports them', async () => {
    const zip = ZipArchive.open(
      await makeZip([
        { name: 'dup.txt', data: 'first' },
        { name: 'dup.txt', data: 'second' },
      ]),
    );
    // Two readers disagreeing about one file is exactly why ZIP is not an MCDF
    // container form. Ingesting a foreign archive, the ambiguity is reported
    // rather than silently resolved.
    expect(zip.duplicates).toEqual(['dup.txt']);
    expect(await zip.readText('dup.txt')).toBe('first');
  });

  it('skips directory entries', async () => {
    const zip = ZipArchive.open(
      await makeZip([
        { name: 'dir/', data: '' },
        { name: 'dir/a.txt', data: 'a' },
      ]),
    );
    expect(zip.list()).toEqual(['dir/a.txt']);
  });

  it('rejects input that is not a ZIP at all', () => {
    expect(() => ZipArchive.open(new TextEncoder().encode('not a zip'))).toThrow(ZipError);
    expect(() => ZipArchive.open(new Uint8Array(4))).toThrow(/too short/);
  });

  it('rejects an archive whose tail was cut off', async () => {
    const whole = await makeZip([{ name: 'a.txt', data: 'hello' }]);
    expect(() => ZipArchive.open(whole.slice(0, whole.length - 10))).toThrow(
      /no end-of-central-directory/,
    );
  });

  it('rejects a central directory pointing past the end of the archive', async () => {
    const bytes = await makeZip([{ name: 'a.txt', data: 'hello' }]);
    const view = new DataView(bytes.buffer);
    const eocd = bytes.length - 22;
    view.setUint32(eocd + 16, bytes.length + 1000, true);
    expect(() => ZipArchive.open(bytes)).toThrow(/extends past the end/);
  });

  it('rejects a member whose data runs past the end of the archive', async () => {
    const bytes = await makeZip([{ name: 'a.txt', data: 'hello' }]);
    const view = new DataView(bytes.buffer);
    view.setUint32(centralOffset(bytes) + 20, 0x0fffffff, true);
    const zip = ZipArchive.open(bytes);
    await expect(zip.read('a.txt')).rejects.toThrow(/extends past the end/);
  });

  it('refuses a member that declares more than the limit', async () => {
    const zip = ZipArchive.open(await makeZip([{ name: 'big.txt', data: 'x'.repeat(4096) }]), {
      maxMemberBytes: 100,
    });
    await expect(zip.read('big.txt')).rejects.toThrow(/over the 100 limit/);
  });

  it('stops a member that inflates past the limit despite its header', async () => {
    // The zip-bomb shape: the header understates the size, so the only defence
    // is counting the bytes as they arrive.
    const bytes = await makeZip([{ name: 'bomb.txt', data: 'a'.repeat(100_000) }]);
    // Understate the size in the central directory, so the cheap up-front check
    // waves it through and only the streaming counter can stop it.
    new DataView(bytes.buffer).setUint32(centralOffset(bytes) + 24, 10, true);
    const zip = ZipArchive.open(bytes, { maxMemberBytes: 1000 });
    await expect(zip.read('bomb.txt')).rejects.toThrow(/inflates past/);
  });

  it('reports a member that is not valid DEFLATE', async () => {
    const bytes = await makeZip([{ name: 'a.txt', data: 'hello' }]);
    // Corrupt the compressed body, which starts after the 30-byte local header
    // and the 5-byte name.
    bytes[30 + 5] = 0xff;
    bytes[30 + 6] = 0xff;
    const zip = ZipArchive.open(bytes);
    await expect(zip.read('a.txt')).rejects.toThrow(ZipError);
  });

  it('refuses an unsupported compression method', async () => {
    const bytes = await makeZip([{ name: 'a.txt', data: 'hello' }]);
    new DataView(bytes.buffer).setUint16(centralOffset(bytes) + 10, 99, true);
    const zip = ZipArchive.open(bytes);
    await expect(zip.read('a.txt')).rejects.toThrow(/unsupported compression method/);
  });
});
