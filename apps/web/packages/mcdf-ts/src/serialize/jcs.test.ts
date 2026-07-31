// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { canonicalizeJson, canonicalizeValue } from './jcs.js';

const SOH = String.fromCharCode(1); // U+0001, a control character JSON must escape

describe('RFC 8785 canonicalization', () => {
  it('sorts object keys and strips insignificant whitespace', () => {
    expect(canonicalizeJson('{ "b": 1, "a": 2 }')).toBe('{"a":2,"b":1}');
  });

  it('sorts by UTF-16 code unit, not by locale', () => {
    expect(canonicalizeValue({ '€': 1, 'é': 2, a: 3 })).toBe(
      '{"a":3,"é":2,"€":1}',
    );
  });

  it('preserves array order', () => {
    expect(canonicalizeJson('[3,1,2]')).toBe('[3,1,2]');
  });

  it('escapes only what JSON requires', () => {
    const s = ['a', '"', 'b', '\\', 'c', '\n', 'd', '\t', 'e', SOH, 'f'].join('');
    expect(canonicalizeValue({ s })).toBe(
      ['{"s":"a', '\\"', 'b', '\\\\', 'c', '\\n', 'd', '\\t', 'e', '\\u0001', 'f"}'].join(''),
    );
  });

  it('leaves non-ASCII unescaped', () => {
    expect(canonicalizeValue({ s: 'café — 中' })).toBe(
      '{"s":"café — 中"}',
    );
  });

  it('serializes numbers per ECMAScript Number::toString', () => {
    expect(canonicalizeValue({ n: 1 })).toBe('{"n":1}');
    expect(canonicalizeValue({ n: 1.5 })).toBe('{"n":1.5}');
    expect(canonicalizeValue({ n: -0 })).toBe('{"n":0}');
    expect(canonicalizeValue({ n: 1e21 })).toBe('{"n":1e+21}');
  });

  it('rejects malformed JSON', () => {
    expect(() => canonicalizeJson('{')).toThrow();
  });
});
