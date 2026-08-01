// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Path and slug helpers shared by the importers, ported from the reference
// implementation's `src/core/xhtml_md.hpp` so a document imported here lands in
// the same shape it would have landed in from the CLI or Studio.
//
// Imports are tooling rather than format (plan: "spec touch: none required"),
// so nothing here is normative and byte-parity with the C++ importer is not
// promised. Matching it anyway costs nothing and means a book imported in the
// browser and the same book imported on the desktop are recognisably the same
// document.

/** Directory part of a path, with its trailing slash, or '' at the root. */
export function dirOf(path: string): string {
  const slash = path.lastIndexOf('/');
  return slash === -1 ? '' : path.slice(0, slash + 1);
}

/**
 * Resolves `href` against `baseDir`, collapsing `.` and `..`.
 *
 * Returns '' for anything that climbs out of the archive root. That is the
 * first line of defence against a hostile EPUB: a chapter referencing
 * `../../../etc/passwd` resolves to nothing and is reported as a broken
 * reference rather than read. `checkMemberPath` is the second line, on the way
 * into the container.
 */
export function resolvePath(baseDir: string, href: string): string {
  const joined = href.startsWith('/') ? href.slice(1) : baseDir + href;
  const parts: string[] = [];
  for (const part of joined.split('/')) {
    if (part === '..') {
      if (parts.length === 0) return '';
      parts.pop();
    } else if (part !== '' && part !== '.') {
      parts.push(part);
    }
  }
  return parts.join('/');
}

export function stripFragment(href: string): string {
  const hash = href.indexOf('#');
  return hash === -1 ? href : href.slice(0, hash);
}

/** True for references that name something outside the document entirely. */
export function isExternal(href: string): boolean {
  return href.includes('://') || href.startsWith('data:') || href.startsWith('mailto:');
}

/**
 * Percent-decodes a URL reference, leaving it unchanged when it is not valid
 * encoding.
 *
 * EPUB hrefs are URLs, so a file named `A Chapter.xhtml` appears as
 * `A%20Chapter.xhtml` and would otherwise never match the archive entry.
 * `decodeURIComponent` throws on a lone `%`, which a hand-built EPUB can
 * contain, and a thrown import is a worse answer than an unresolved image.
 */
export function decodeHref(href: string): string {
  try {
    return decodeURIComponent(href);
  } catch {
    return href;
  }
}

/**
 * Slug for heading anchors and asset names: lowercase alphanumerics, runs of
 * anything else collapsed to a single dash, capped at 48 characters.
 *
 * `\p{Nd}` and `\p{L}` rather than ASCII-only, because the C++ version's
 * `isalnum` is byte-wise and would reduce a Japanese or Greek chapter title to
 * the fallback. An id is matched literally against `schema.yaml`, never parsed
 * as Markdown, so a non-ASCII id is legal — spec §4.2 constrains where an
 * anchor may sit, not which characters it may use.
 */
export function slugify(text: string, fallback: string): string {
  let out = '';
  let dash = false;
  for (const ch of text) {
    if (/[\p{L}\p{Nd}]/u.test(ch)) {
      out += ch.toLowerCase();
      dash = false;
    } else if (out !== '' && !dash) {
      out += '-';
      dash = true;
    }
  }
  out = out.replace(/-+$/, '');
  if (out === '') return fallback;
  // Slice by code point, not code unit: cutting at 48 UTF-16 units can land
  // between a surrogate pair and produce a lone surrogate, which is not
  // well-formed UTF-8 and would make the member path unencodable.
  const points = [...out];
  if (points.length > 48) out = points.slice(0, 48).join('');
  return out.replace(/-+$/, '') || fallback;
}

/** Splits a file name into its stem and extension (`.png`, or '' when absent). */
export function splitExtension(name: string): { stem: string; ext: string } {
  const base = name.split('/').pop() ?? name;
  const dot = base.lastIndexOf('.');
  // A leading dot is part of the name, not an extension separator.
  if (dot <= 0) return { stem: base, ext: '' };
  const ext = base.slice(dot);
  // Anything that is not a plain extension is not treated as one, so a name
  // like `figure.v2 final` cannot smuggle a space or a slash into the member.
  return /^\.[A-Za-z0-9]{1,8}$/.test(ext)
    ? { stem: base.slice(0, dot), ext: ext.toLowerCase() }
    : { stem: base, ext: '' };
}

/**
 * The container path an imported asset lands at: `assets/<slug><ext>`, with a
 * numeric suffix when that name is already taken.
 *
 * `taken` is consulted and updated by the caller across a whole import, so two
 * chapters referencing `images/logo.png` from different directories do not
 * silently overwrite each other.
 */
export function assetPath(source: string, taken: Set<string>): string {
  const { stem, ext } = splitExtension(source);
  const base = `assets/${slugify(stem, 'image')}`;
  let candidate = `${base}${ext}`;
  for (let n = 2; taken.has(candidate); n++) candidate = `${base}-${n}${ext}`;
  taken.add(candidate);
  return candidate;
}
