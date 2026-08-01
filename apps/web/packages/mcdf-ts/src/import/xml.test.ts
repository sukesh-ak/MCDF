// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { collapse, decodeEntities, parseXmlElements } from './xml.js';

const find = (source: string, name: string) =>
  parseXmlElements(source).filter((el) => el.name === name);

describe('decodeEntities', () => {
  it('decodes the predefined entities and numeric references', () => {
    expect(decodeEntities('a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;')).toBe(
      `a & b <c> "d" 'e'`,
    );
    expect(decodeEntities('&#65;&#x42;')).toBe('AB');
  });

  it('leaves an undeclared entity visible rather than dropping it', () => {
    expect(decodeEntities('a &unknown; b')).toBe('a &unknown; b');
  });

  it('refuses a numeric reference to a surrogate or out of range', () => {
    // fromCodePoint would happily produce a lone surrogate, which cannot be
    // encoded as UTF-8 and would poison whatever member the text lands in.
    expect(decodeEntities('&#xD800;')).toBe('&#xD800;');
    expect(decodeEntities('&#x110000;')).toBe('&#x110000;');
  });
});

describe('parseXmlElements', () => {
  it('reads element names without their namespace prefix', () => {
    const els = find('<package><dc:title>Moby-Dick</dc:title></package>', 'title');
    expect(els).toHaveLength(1);
    expect(els[0]?.text).toBe('Moby-Dick');
  });

  it('reads attributes in either quoting style and decodes them', () => {
    const [item] = find(`<item id='a' href="x&amp;y.xhtml" media-type='text/html'/>`, 'item');
    expect(item?.attrs).toMatchObject({
      id: 'a',
      href: 'x&y.xhtml',
      'media-type': 'text/html',
    });
  });

  it('does not end a tag on a > inside a quoted attribute', () => {
    // Rare and legal. Getting it wrong truncates an href rather than failing,
    // which surfaces as one mysteriously missing image.
    const [item] = find('<item href="a>b.xhtml" id="x"/>', 'item');
    expect(item?.attrs['href']).toBe('a>b.xhtml');
    expect(item?.attrs['id']).toBe('x');
  });

  it('skips comments, processing instructions and CDATA markup', () => {
    const source = `<?xml version="1.0"?><!-- <title>decoy</title> --><r><title><![CDATA[A & B]]></title></r>`;
    const titles = find(source, 'title');
    expect(titles).toHaveLength(1);
    // Entities are not expanded inside CDATA.
    expect(titles[0]?.text).toBe('A & B');
  });

  it('skips a DOCTYPE with an internal subset containing >', () => {
    const source = `<!DOCTYPE package [ <!ENTITY x "a>b"> ]><package><dc:title>T</dc:title></package>`;
    expect(find(source, 'title')[0]?.text).toBe('T');
  });

  it('collects descendant text, so a marked-up title still reads', () => {
    const [title] = find('<title>Moby-<em>Dick</em></title>', 'title');
    expect(title?.text).toBe('Moby-Dick');
  });

  it('keeps document order, which is what spine order depends on', () => {
    const source = '<spine><itemref idref="a"/><itemref idref="b"/><itemref idref="c"/></spine>';
    expect(find(source, 'itemref').map((el) => el.attrs['idref'])).toEqual(['a', 'b', 'c']);
  });

  it('does not unwind the stack on a stray closing tag', () => {
    const [title] = find('<r><title>A</p>B</title></r>', 'title');
    expect(title?.text).toBe('AB');
  });

  it('survives a truncated document', () => {
    // A cut-off OPF yields what was readable; the importer reports what it
    // could not find rather than throwing from the parser.
    expect(() => parseXmlElements('<package><dc:title>Half')).not.toThrow();
    expect(find('<package><dc:title>Half', 'title')[0]?.text).toBe('Half');
  });

  it('rejects absurd nesting rather than recursing on it', () => {
    expect(() => parseXmlElements('<a>'.repeat(200))).toThrow(/too deep/);
  });
});

describe('collapse', () => {
  it('flattens runs of whitespace and trims', () => {
    expect(collapse('  Moby\n  Dick  ')).toBe('Moby Dick');
  });
});
