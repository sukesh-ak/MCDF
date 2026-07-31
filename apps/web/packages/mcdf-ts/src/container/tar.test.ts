// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { utf8Decode, utf8Encode } from '../util/bytes.js';
import { TarError, tarRead, tarWrite } from './tar.js';

const member = (path: string, text: string) => ({ path, data: utf8Encode(text) });

describe('tarWrite', () => {
  it('round-trips members', () => {
    const bytes = tarWrite([member('content.md', '# Hi\n'), member('metadata.yaml', 'title: Hi\n')]);
    const back = tarRead(bytes);
    expect(back.map((m) => m.path)).toEqual(['content.md', 'metadata.yaml']);
    expect(utf8Decode(back[0]!.data)).toBe('# Hi\n');
  });

  it('is deterministic regardless of input order', () => {
    const a = tarWrite([member('b.md', 'b'), member('a.md', 'a')]);
    const b = tarWrite([member('a.md', 'a'), member('b.md', 'b')]);
    expect(a).toEqual(b);
  });

  it('emits 512-byte blocks and two trailing zero blocks', () => {
    const bytes = tarWrite([member('a.md', 'x')]);
    // header + one data block + two zero blocks
    expect(bytes.length).toBe(512 * 4);
    expect(bytes.subarray(512 * 2).every((b) => b === 0)).toBe(true);
  });

  it('normalises mtime, uid, gid and mode', () => {
    const header = tarWrite([member('a.md', 'x')]).subarray(0, 512);
    const field = (off: number, len: number) => utf8Decode(header.subarray(off, off + len - 1));
    expect(field(100, 8)).toBe('0000644'); // mode
    expect(field(108, 8)).toBe('0000000'); // uid
    expect(field(116, 8)).toBe('0000000'); // gid
    expect(field(136, 12)).toBe('00000000000'); // mtime
    expect(utf8Decode(header.subarray(257, 262))).toBe('ustar');
    expect(header[156]).toBe(0x30); // regular file
  });

  it('pads member data to a block boundary', () => {
    const bytes = tarWrite([member('a.md', 'x'.repeat(513))]);
    expect(bytes.length).toBe(512 * 5); // header + 2 data blocks + 2 zero blocks
  });

  it('handles paths that need the ustar prefix field', () => {
    const long = `${'d'.repeat(80)}/${'e'.repeat(60)}/file.md`;
    const back = tarRead(tarWrite([member(long, 'x')]));
    expect(back[0]!.path).toBe(long);
  });

  it('rejects paths that do not fit ustar', () => {
    expect(() => tarWrite([member('x'.repeat(300), 'x')])).toThrow(TarError);
  });
});

describe('tarRead', () => {
  it('rejects a corrupted header checksum', () => {
    const bytes = tarWrite([member('a.md', 'x')]);
    bytes[0] = 0x7a; // change the name; the stored checksum no longer matches
    expect(() => tarRead(bytes)).toThrow(TarError);
  });

  it('rejects truncated data', () => {
    const bytes = tarWrite([member('a.md', 'x'.repeat(600))]);
    expect(() => tarRead(bytes.subarray(0, 512 + 512))).toThrow(TarError);
  });

  it('reads an empty archive', () => {
    expect(tarRead(new Uint8Array(1024))).toEqual([]);
  });
});
