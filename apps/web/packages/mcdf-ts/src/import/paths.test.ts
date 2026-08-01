// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import {
  assetPath,
  decodeHref,
  dirOf,
  isExternal,
  resolvePath,
  slugify,
  splitExtension,
  stripFragment,
} from './paths.js';

describe('dirOf', () => {
  it('keeps the trailing slash and is empty at the root', () => {
    expect(dirOf('OEBPS/text/ch1.xhtml')).toBe('OEBPS/text/');
    expect(dirOf('content.opf')).toBe('');
  });
});

describe('resolvePath', () => {
  it('resolves relative references against the document directory', () => {
    expect(resolvePath('OEBPS/text/', '../images/fig.png')).toBe('OEBPS/images/fig.png');
    expect(resolvePath('OEBPS/text/', './fig.png')).toBe('OEBPS/text/fig.png');
    expect(resolvePath('', 'content.opf')).toBe('content.opf');
  });

  it('treats a leading slash as archive-root relative', () => {
    expect(resolvePath('OEBPS/text/', '/images/fig.png')).toBe('images/fig.png');
  });

  it('refuses anything that climbs out of the archive', () => {
    // The first line of defence against a hostile archive: a reference that
    // escapes resolves to nothing and is reported, never read.
    expect(resolvePath('OEBPS/', '../../etc/passwd')).toBe('');
    expect(resolvePath('', '../secret')).toBe('');
  });
});

describe('stripFragment and isExternal', () => {
  it('drops the fragment', () => {
    expect(stripFragment('ch1.xhtml#part2')).toBe('ch1.xhtml');
    expect(stripFragment('ch1.xhtml')).toBe('ch1.xhtml');
  });

  it('recognises references that leave the document', () => {
    expect(isExternal('https://example.com/a.png')).toBe(true);
    expect(isExternal('data:image/png;base64,AAA')).toBe(true);
    expect(isExternal('mailto:someone@example.com')).toBe(true);
    expect(isExternal('images/fig.png')).toBe(false);
  });
});

describe('decodeHref', () => {
  it('percent-decodes', () => {
    expect(decodeHref('A%20Chapter.xhtml')).toBe('A Chapter.xhtml');
  });

  it('leaves invalid encoding alone rather than throwing', () => {
    // A hand-built EPUB can contain a lone '%'. An unresolved image beats a
    // thrown import.
    expect(decodeHref('100%.xhtml')).toBe('100%.xhtml');
  });
});

describe('slugify', () => {
  it('lowercases and collapses separators', () => {
    expect(slugify('Chapter One: The Beginning', 'x')).toBe('chapter-one-the-beginning');
    expect(slugify('  spaced  out  ', 'x')).toBe('spaced-out');
  });

  it('falls back when nothing survives', () => {
    expect(slugify('!!!', 'chapter-3')).toBe('chapter-3');
    expect(slugify('', 'fallback')).toBe('fallback');
  });

  it('keeps non-Latin letters instead of reducing them to the fallback', () => {
    // The C++ version's byte-wise isalnum drops these; an id is matched
    // literally against schema.yaml and never parsed, so they are legal.
    expect(slugify('日本語の章', 'x')).toBe('日本語の章');
    expect(slugify('Πρόλογος', 'x')).toBe('πρόλογος');
  });

  it('caps length without splitting a surrogate pair', () => {
    const slug = slugify('𝄞'.repeat(60), 'x');
    expect([...slug].length).toBeLessThanOrEqual(48);
    // A lone surrogate cannot be encoded as UTF-8 and would poison the member.
    expect(slug).toBe(new TextDecoder().decode(new TextEncoder().encode(slug)));
  });
});

describe('splitExtension', () => {
  it('splits an ordinary name', () => {
    expect(splitExtension('images/fig.PNG')).toEqual({ stem: 'fig', ext: '.png' });
  });

  it('treats a leading dot as part of the name', () => {
    expect(splitExtension('.hidden')).toEqual({ stem: '.hidden', ext: '' });
  });

  it('refuses to call something an extension when it is not one', () => {
    expect(splitExtension('figure.v2 final')).toEqual({ stem: 'figure.v2 final', ext: '' });
  });
});

describe('assetPath', () => {
  it('lands under assets/ with a slugged name', () => {
    expect(assetPath('OEBPS/images/Figure One.png', new Set())).toBe('assets/figure-one.png');
  });

  it('does not let two sources collide', () => {
    // Chapter 1's images/logo.png and chapter 2's pics/logo.png are different
    // files with the same basename; silently overwriting one is data loss.
    const taken = new Set<string>();
    expect(assetPath('a/logo.png', taken)).toBe('assets/logo.png');
    expect(assetPath('b/logo.png', taken)).toBe('assets/logo-2.png');
    expect(assetPath('c/logo.png', taken)).toBe('assets/logo-3.png');
  });
});
