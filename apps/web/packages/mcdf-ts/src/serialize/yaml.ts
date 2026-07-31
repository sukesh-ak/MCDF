// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// YAML members: parsed tolerantly with js-yaml, but *emitted* by a hand-rolled
// writer ported from `src/serialize/writers.cpp`. Emission has to be
// byte-identical to the reference implementation — a document round-tripped
// through this client must produce the same manifest hashes as one round-tripped
// through the C++ CLI — and no general YAML emitter guarantees that layout.

import yaml from 'js-yaml';

import {
  emptyMetadata,
  emptySchema,
  type DocumentSchema,
  type Metadata,
  type SchemaSection,
} from '../model/types.js';

export class ParseError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ParseError';
  }
}

function load(text: string, member: string): unknown {
  try {
    return yaml.load(text, { schema: yaml.JSON_SCHEMA });
  } catch (e) {
    throw new ParseError(`${member}: ${(e as Error).message}`);
  }
}

function asRecord(value: unknown): Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : {};
}

/** Reads a scalar as a string, mirroring yaml-cpp's permissive `as<string>()`. */
function str(node: Record<string, unknown>, key: string): string {
  const v = node[key];
  if (v === undefined || v === null) return '';
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  return '';
}

function asArray(value: unknown): unknown[] {
  return Array.isArray(value) ? value : [];
}

export function parseMetadataYaml(text: string): Metadata {
  const n = asRecord(load(text, 'metadata.yaml'));
  const m = emptyMetadata();
  m.title = str(n, 'title');
  m.version = str(n, 'version');
  for (const raw of asArray(n['authors'])) {
    const a = asRecord(raw);
    m.authors.push({ name: str(a, 'name'), id: str(a, 'id') });
  }
  m.created_at = str(n, 'created_at');
  m.classification = str(n, 'classification');
  m.language = str(n, 'language');
  if (n['generated_by'] !== undefined && n['generated_by'] !== null) {
    m.generated_by = str(n, 'generated_by');
  }
  return m;
}

export function parseSchemaYaml(text: string): DocumentSchema {
  const n = asRecord(load(text, 'schema.yaml'));
  const s = emptySchema();
  s.document_type = str(n, 'document_type');
  for (const raw of asArray(n['sections'])) {
    const sec = asRecord(raw);
    const section: SchemaSection = {
      id: str(sec, 'id'),
      title: str(sec, 'title'),
      required: sec['required'] === true,
    };
    s.sections.push(section);
  }
  return s;
}

// ---------------------------------------------------------------------------
// Emission — ported from writers.cpp, character for character.
// ---------------------------------------------------------------------------

const RESERVED_WORDS = new Set(['true', 'false', 'yes', 'no', 'on', 'off', 'null', '~']);

function isSpace(ch: string): boolean {
  return ch === ' ' || ch === '\t' || ch === '\n' || ch === '\v' || ch === '\f' || ch === '\r';
}

function isDigit(ch: string): boolean {
  return ch >= '0' && ch <= '9';
}

/**
 * True when `s` is safe as a plain (unquoted) YAML scalar. Deliberately
 * conservative: anything numeric-looking, date-like, or carrying a YAML
 * indicator gets double-quoted instead.
 */
function isPlainSafe(s: string): boolean {
  if (s === '') return false;
  const first = s[0] as string;
  if (isSpace(first) || isSpace(s[s.length - 1] as string)) return false;
  if ('-?:,[]{}#&*!|>\'"%@`'.includes(first)) return false;
  if (isDigit(first)) return false;
  if ((first === '+' || first === '.') && s.length > 1 && isDigit(s[1] as string)) return false;
  for (const ch of s) {
    if ((ch.codePointAt(0) as number) < 0x20) return false;
    if (ch === ':' || ch === '#' || ch === '"' || ch === '\\') return false;
  }
  return !RESERVED_WORDS.has(s.toLowerCase());
}

function doubleQuoted(s: string): string {
  let out = '"';
  for (const ch of s) {
    switch (ch) {
      case '\\': out += '\\\\'; break;
      case '"': out += '\\"'; break;
      case '\n': out += '\\n'; break;
      case '\r': out += '\\r'; break;
      case '\t': out += '\\t'; break;
      default: {
        const c = ch.codePointAt(0) as number;
        if (c < 0x20) {
          out += `\\x${c.toString(16).toUpperCase().padStart(2, '0')}`;
        } else {
          out += ch;
        }
      }
    }
  }
  return `${out}"`;
}

function scalar(s: string): string {
  return isPlainSafe(s) ? s : doubleQuoted(s);
}

export function metadataToYaml(metadata: Metadata): string {
  let out = '';
  out += `title: ${scalar(metadata.title)}\n`;
  out += `version: ${scalar(metadata.version)}\n`;
  if (metadata.authors.length === 0) {
    out += 'authors: []\n';
  } else {
    out += 'authors:\n';
    for (const a of metadata.authors) {
      out += `  - name: ${scalar(a.name)}\n`;
      out += `    id: ${scalar(a.id)}\n`;
    }
  }
  out += `created_at: ${scalar(metadata.created_at)}\n`;
  out += `classification: ${scalar(metadata.classification)}\n`;
  out += `language: ${scalar(metadata.language)}\n`;
  if (metadata.generated_by !== undefined) {
    out += `generated_by: ${scalar(metadata.generated_by)}\n`;
  }
  return out;
}

export function schemaToYaml(schema: DocumentSchema): string {
  let out = '';
  // document_type is optional (spec §4.2) and MUST NOT be present-but-empty:
  // absence is how a document declines to state a type, and emitting
  // `document_type: ""` would produce a schema.yaml that fails the JSON Schema
  // this same writer's output is supposed to satisfy.
  if (schema.document_type !== '') {
    out += `document_type: ${scalar(schema.document_type)}\n`;
  }
  if (schema.sections.length === 0) {
    out += 'sections: []\n';
  } else {
    out += 'sections:\n';
    for (const s of schema.sections) {
      out += `  - id: ${scalar(s.id)}\n`;
      out += `    title: ${scalar(s.title)}\n`;
      out += `    required: ${s.required ? 'true' : 'false'}\n`;
    }
  }
  return out;
}

/**
 * Normalises `content.md` before it is written: CRLF/CR line endings become LF,
 * trailing blank lines collapse to a single newline, and the file always ends
 * with one. Keeps hashes stable across editors and platforms.
 */
export function canonicalizeContent(text: string): string {
  let out = '';
  for (let i = 0; i < text.length; i++) {
    if (text[i] === '\r') {
      out += '\n';
      if (i + 1 < text.length && text[i + 1] === '\n') i++; // CRLF -> LF
    } else {
      out += text[i];
    }
  }
  while (out.length >= 2 && out.endsWith('\n\n')) out = out.slice(0, -1);
  if (out !== '' && !out.endsWith('\n')) out += '\n';
  return out;
}
