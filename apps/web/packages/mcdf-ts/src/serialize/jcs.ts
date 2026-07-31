// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// RFC 8785 (JCS) canonical JSON. Signatures are computed over the canonical
// manifest, so this output is normative: reformatting `manifest.json` must not
// invalidate a signature, but changing a hash must.

export type JsonValue =
  | null
  | boolean
  | number
  | string
  | JsonValue[]
  | { [key: string]: JsonValue };

export class JcsError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'JcsError';
  }
}

const ESCAPES: Record<number, string> = {
  0x08: '\\b',
  0x09: '\\t',
  0x0a: '\\n',
  0x0c: '\\f',
  0x0d: '\\r',
  0x22: '\\"',
  0x5c: '\\\\',
};

function writeEscaped(s: string, out: string[]): void {
  out.push('"');
  for (const ch of s) {
    const c = ch.codePointAt(0) as number;
    const esc = ESCAPES[c];
    if (esc !== undefined) {
      out.push(esc);
    } else if (c < 0x20) {
      out.push('\\u00', c.toString(16).padStart(2, '0'));
    } else {
      out.push(ch);
    }
  }
  out.push('"');
}

function writeNumber(n: number, out: string[]): void {
  if (!Number.isFinite(n)) {
    throw new JcsError(`number is not representable in JSON: ${n}`);
  }
  // RFC 8785 §3.2.2.3 defines number serialization as ECMAScript
  // `Number::toString`, which is exactly what `String(n)` produces — including
  // "-0" normalising to "0" and the e+21 exponent threshold.
  out.push(Object.is(n, -0) ? '0' : String(n));
}

function write(value: JsonValue, out: string[]): void {
  if (value === null) {
    out.push('null');
    return;
  }
  switch (typeof value) {
    case 'boolean':
      out.push(value ? 'true' : 'false');
      return;
    case 'number':
      writeNumber(value, out);
      return;
    case 'string':
      writeEscaped(value, out);
      return;
  }
  if (Array.isArray(value)) {
    out.push('[');
    value.forEach((item, i) => {
      if (i > 0) out.push(',');
      write(item, out);
    });
    out.push(']');
    return;
  }
  // Object keys sort by UTF-16 code unit — which is precisely what JavaScript's
  // default string comparison does, so no custom comparator is needed here (it
  // is needed for TAR member order, which sorts by UTF-8 bytes instead).
  const keys = Object.keys(value).sort();
  out.push('{');
  keys.forEach((k, i) => {
    if (i > 0) out.push(',');
    writeEscaped(k, out);
    out.push(':');
    write(value[k] as JsonValue, out);
  });
  out.push('}');
}

/** Serializes a JSON value in RFC 8785 canonical form. */
export function canonicalizeValue(value: JsonValue): string {
  const out: string[] = [];
  write(value, out);
  return out.join('');
}

/** Re-serializes existing JSON text in RFC 8785 canonical form. */
export function canonicalizeJson(text: string): string {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch (e) {
    throw new JcsError(`canonicalize: ${(e as Error).message}`);
  }
  return canonicalizeValue(parsed as JsonValue);
}
