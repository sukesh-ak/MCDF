// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// A minimal XML element scanner, for EPUB's `META-INF/container.xml` and its
// package (OPF) file.
//
// Why not `DOMParser`: it does not exist in Node, and this package's headline
// property is that the same build runs in Node, a browser and a worker. The
// alternative was injecting a parser at every call site or taking a DOM
// implementation as a dependency; both cost more than the ~120 lines below,
// because of what these two files actually need — element names, attributes and
// leaf text. This is the same call `mcdf_micro` makes with `yaml_min.c`: a
// reader does not need a conforming parser for the questions it asks.
//
// The chapters are a different matter and are NOT parsed here — they go through
// turndown, which uses the platform's real HTML parser. Nothing in this file
// ever sees document content.

/** Depth cap. Nothing legitimate in an OPF nests remotely this far. */
const MAX_DEPTH = 100;

export class XmlError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'XmlError';
  }
}

export interface XmlElement {
  /** Element name with any namespace prefix removed, lowercased. */
  name: string;
  /** Attributes keyed by local name, lowercased. */
  attrs: Record<string, string>;
  /** Concatenated character data of this element and its descendants. */
  text: string;
}

const ENTITIES = new Map([
  ['amp', '&'],
  ['lt', '<'],
  ['gt', '>'],
  ['quot', '"'],
  ['apos', "'"],
  // Not XML-predefined, but pervasive in real-world EPUB metadata, and the
  // failure mode of leaving it raw is a literal "&nbsp;" in a book title.
  ['nbsp', ' '],
]);

export function decodeEntities(text: string): string {
  if (!text.includes('&')) return text;
  return text.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);/g, (whole, body: string) => {
    if (body.startsWith('#x') || body.startsWith('#X')) {
      const code = Number.parseInt(body.slice(2), 16);
      return isCodePoint(code) ? String.fromCodePoint(code) : whole;
    }
    if (body.startsWith('#')) {
      const code = Number.parseInt(body.slice(1), 10);
      return isCodePoint(code) ? String.fromCodePoint(code) : whole;
    }
    // An entity nobody declared is left exactly as written rather than dropped:
    // a visible "&unknown;" in a title is a bug someone can see and report.
    return ENTITIES.get(body.toLowerCase()) ?? whole;
  });
}

function isCodePoint(code: number): boolean {
  // Surrogates are excluded: `String.fromCodePoint` accepts them individually
  // and produces a lone surrogate, which cannot be encoded as UTF-8 and would
  // poison whatever member the text lands in.
  return (
    Number.isFinite(code) && code >= 0 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff)
  );
}

/** Local name of `dc:title` / `DC:Title` — the `local-name()` the C++ side matches on. */
function localName(raw: string): string {
  const colon = raw.indexOf(':');
  return (colon === -1 ? raw : raw.slice(colon + 1)).toLowerCase();
}

/**
 * Scans an XML document into a flat list of elements, in document order.
 *
 * Flat rather than a tree because every question these files are asked is a
 * flat one — "every `item` element", "the first `title`" — and the C++ importer
 * asks them with `//*[local-name()='…']`, which is also a flat scan. A tree
 * would be more code for no additional answer.
 */
export function parseXmlElements(source: string): XmlElement[] {
  const elements: XmlElement[] = [];
  const open: XmlElement[] = [];
  let at = 0;

  const addText = (text: string): void => {
    if (text === '' || open.length === 0) return;
    for (const element of open) element.text += text;
  };

  while (at < source.length) {
    const lt = source.indexOf('<', at);
    if (lt === -1) {
      addText(decodeEntities(source.slice(at)));
      break;
    }
    addText(decodeEntities(source.slice(at, lt)));

    if (source.startsWith('<!--', lt)) {
      const end = source.indexOf('-->', lt + 4);
      at = end === -1 ? source.length : end + 3;
      continue;
    }
    if (source.startsWith('<![CDATA[', lt)) {
      const end = source.indexOf(']]>', lt + 9);
      // Character data verbatim: entities are not expanded inside CDATA.
      addText(source.slice(lt + 9, end === -1 ? source.length : end));
      at = end === -1 ? source.length : end + 3;
      continue;
    }
    if (source.startsWith('<?', lt)) {
      const end = source.indexOf('?>', lt + 2);
      at = end === -1 ? source.length : end + 2;
      continue;
    }
    if (source.startsWith('<!', lt)) {
      at = skipDeclaration(source, lt);
      continue;
    }

    const gt = findTagEnd(source, lt);
    if (gt === -1) {
      // An unterminated tag at EOF. Everything already scanned stands; a
      // truncated OPF is reported by the importer as missing what it needed.
      break;
    }
    const inner = source.slice(lt + 1, gt);
    at = gt + 1;

    if (inner.startsWith('/')) {
      const name = localName(inner.slice(1).trim());
      // Pop to the nearest matching open element rather than assuming the
      // document is balanced; a stray `</p>` must not unwind the whole stack.
      for (let i = open.length - 1; i >= 0; i--) {
        if (open[i]?.name === name) {
          open.length = i;
          break;
        }
      }
      continue;
    }

    const selfClosing = inner.endsWith('/');
    const body = selfClosing ? inner.slice(0, -1) : inner;
    const element = parseTag(body);
    if (element === null) continue;
    elements.push(element);
    if (!selfClosing) {
      if (open.length >= MAX_DEPTH) throw new XmlError('XML nesting is too deep');
      open.push(element);
    }
  }

  return elements;
}

/**
 * End of a tag, skipping `>` that appears inside a quoted attribute value.
 *
 * `<item href="a>b"/>` is legal and rare; getting it wrong truncates an href
 * rather than failing, which is the kind of defect that surfaces as one
 * mysteriously missing image.
 */
function findTagEnd(source: string, from: number): number {
  let quote = '';
  for (let i = from + 1; i < source.length; i++) {
    const ch = source[i];
    if (quote !== '') {
      if (ch === quote) quote = '';
    } else if (ch === '"' || ch === "'") {
      quote = ch;
    } else if (ch === '>') {
      return i;
    }
  }
  return -1;
}

/**
 * Skips `<!DOCTYPE …>`, including an internal subset.
 *
 * The subset is bracketed and may contain `>` freely, so scanning for the first
 * `>` would stop inside it and leave the rest of the declaration to be parsed
 * as elements.
 */
function skipDeclaration(source: string, from: number): number {
  let depth = 0;
  let quote = '';
  for (let i = from + 2; i < source.length; i++) {
    const ch = source[i];
    if (quote !== '') {
      if (ch === quote) quote = '';
      continue;
    }
    if (ch === '"' || ch === "'") quote = ch;
    else if (ch === '[') depth++;
    else if (ch === ']') depth--;
    else if (ch === '>' && depth <= 0) return i + 1;
  }
  return source.length;
}

const ATTR = /([^\s=/]+)\s*=\s*("([^"]*)"|'([^']*)'|([^\s"'>]+))/g;

function parseTag(body: string): XmlElement | null {
  const trimmed = body.trim();
  if (trimmed === '') return null;
  const space = trimmed.search(/\s/);
  const rawName = space === -1 ? trimmed : trimmed.slice(0, space);
  const name = localName(rawName);
  if (name === '') return null;

  const attrs: Record<string, string> = Object.create(null) as Record<string, string>;
  if (space !== -1) {
    const rest = trimmed.slice(space);
    ATTR.lastIndex = 0;
    for (let m = ATTR.exec(rest); m !== null; m = ATTR.exec(rest)) {
      const key = localName(m[1] ?? '');
      const value = m[3] ?? m[4] ?? m[5] ?? '';
      // First wins, matching XML's "duplicate attributes are an error" being
      // resolved leniently rather than by taking the later, attacker-appended one.
      if (key !== '' && !(key in attrs)) attrs[key] = decodeEntities(value);
    }
  }
  return { name, attrs, text: '' };
}

/** Collapses runs of whitespace and trims — the shape metadata text wants. */
export function collapse(text: string): string {
  return text.replace(/\s+/g, ' ').trim();
}
