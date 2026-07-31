// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// CommonMark: heading extraction (structure binding) and HTML rendering
// (preview). The reference implementation uses md4c; here micromark plays the
// same role, and heading text is collected from the *text* tokens so inline
// markup is stripped the way md4c's text callback strips it.

import { micromark } from 'micromark';

import type { Heading } from '../model/types.js';

interface CompileContext {
  sliceSerialize(token: unknown): string;
}

/** The subset of a micromark token this module reads. */
interface PositionedToken {
  start?: { line?: number };
}

type Handle = (this: CompileContext, token: unknown) => void;

interface HtmlExtensionShape {
  enter?: Record<string, Handle>;
  exit?: Record<string, Handle>;
}

/** Splits `Title {#id}` into text + id (id is '' when absent). */
export function splitHeadingId(raw: string): { text: string; id: string } {
  let text = raw.trim();
  let id = '';
  if (text.endsWith('}')) {
    const pos = text.lastIndexOf('{#');
    if (pos !== -1) {
      id = text.slice(pos + 2, text.length - 1);
      text = text.slice(0, pos).trim();
    }
  }
  return { text, id };
}

/**
 * Parses the headings of a CommonMark document, with their `{#id}` anchors.
 *
 * Runs micromark's compiler with an extension that intercepts heading tokens;
 * the HTML it produces along the way is discarded. Doing it through the real
 * parser (rather than scanning lines) is what keeps `#` inside fenced code
 * blocks, link text and HTML blocks from being mistaken for headings.
 */
export function parseHeadings(markdown: string): Heading[] {
  const headings: Heading[] = [];
  let level = 0;
  let line = 0;
  let parts: string[] | null = null;
  // A link's URL and title are tokenized as data too. `# [Overview](x)` must
  // yield "Overview", not "Overviewx", so collection pauses inside the resource
  // and reference parts the way md4c's text callback never sees them at all.
  let inDestination = 0;

  const open = (token: unknown): void => {
    level = 0;
    // The line number is what lets callers act on a heading — jump the caret to
    // it, or write a `{#id}` anchor onto it — instead of searching the text for
    // a string that may well appear more than once.
    line = (token as PositionedToken).start?.line ?? 0;
    parts = [];
  };

  const close = (): void => {
    if (parts === null) return;
    const { text, id } = splitHeadingId(parts.join(''));
    headings.push({ level, text, id, line });
    parts = null;
    inDestination = 0;
  };

  const collect: Handle = function (token) {
    if (parts !== null && inDestination === 0) parts.push(this.sliceSerialize(token));
  };

  const enterDestination = (): void => {
    inDestination++;
  };

  const exitDestination = (): void => {
    if (inDestination > 0) inDestination--;
  };

  const extension: HtmlExtensionShape = {
    enter: {
      atxHeading: open,
      setextHeading: open,
      resource: enterDestination,
      reference: enterDestination,
    },
    exit: {
      resource: exitDestination,
      reference: exitDestination,
      atxHeadingSequence(token) {
        // Fires for both the opening and the closing `###` run; only the first
        // carries the level.
        if (parts !== null && level === 0) level = this.sliceSerialize(token).length;
      },
      setextHeadingLineSequence(token) {
        if (parts !== null) level = this.sliceSerialize(token).startsWith('=') ? 1 : 2;
      },
      atxHeading: close,
      setextHeading: close,
      data: collect,
      characterEscapeValue: collect,
      codeTextData: collect,
    },
  };

  micromark(markdown, {
    // The extension replaces the default text handlers, so the compiled string
    // is intentionally unused.
    htmlExtensions: [extension as never],
  });

  return headings;
}

/**
 * Removes a trailing ` {#id}` anchor from already-rendered heading text.
 *
 * This replaced a pass that rewrote the *source* line by line, before parsing.
 * A line scan cannot tell a heading from anything else beginning with `#`, so it
 * stripped the anchor out of `# Fake heading {#nope}` inside a fenced code block
 * — the renderer quietly altering a code sample — while missing every heading
 * that does not begin its line (any heading in a blockquote or list) and every
 * setext heading. Working from the parsed heading fixes all three.
 */
function stripTrailingAnchor(text: string, id: string): string {
  const anchor = `{#${id}}`;
  if (!text.endsWith(anchor)) return text;
  return text.slice(0, text.length - anchor.length).replace(/[ \t]+$/, '');
}

/**
 * Writes a `{#id}` anchor onto the heading at `line`, replacing any anchor it
 * already carries.
 *
 * This is the repair for `E_SCHEMA_UNBOUND` and `E_REQUIRED_SECTION_MISSING`:
 * the schema declares a section, and the heading it should bind to needs the
 * matching anchor. Addressing the heading by line (rather than by its text)
 * matters because heading text repeats — "Overview" may appear three times.
 *
 * Returns the content unchanged when the line is out of range or is not a
 * heading, so a stale line number can never corrupt the document.
 */
export function setHeadingAnchor(content: string, line: number, id: string): string {
  const lines = content.split('\n');
  if (line < 1 || line > lines.length) return content;

  const original = lines[line - 1] as string;
  const trimmed = original.trimStart();
  // Setext headings are underlined, so the anchor still belongs on the text line
  // above; either way the target line must not be the underline itself.
  if (/^[=-]+\s*$/.test(trimmed) && trimmed !== '') return content;

  const stripped = stripHeadingIdFrom(original);
  const trailing = /\s*$/.exec(stripped)?.[0] ?? '';
  let body = stripped.slice(0, stripped.length - trailing.length);

  // Drop an optional closing `##` run. Putting the anchor before it
  // (`## Two {#two} ##`) parses correctly but renders badly: `strip_id` in the
  // reference renderer only removes an anchor when the line *ends* with `}`, so
  // the reader would see the raw `{#two}`. Putting it after breaks the anchor
  // entirely, because the run is then no longer a closing sequence. Removing the
  // decoration is the only option that leaves a document every implementation
  // agrees on — and the closing run is purely cosmetic in ATX headings.
  body = body.replace(/\s+#+$/, '');
  if (body.trim() === '') return content;
  return replaceLine(lines, line, `${body} {#${id}}${trailing}`);
}

function replaceLine(lines: string[], line: number, text: string): string {
  const copy = [...lines];
  copy[line - 1] = text;
  return copy.join('\n');
}

/** Removes an existing `{#id}` from a line, heading or not. */
function stripHeadingIdFrom(text: string): string {
  const match = /\s*\{#[^}]*\}(\s*#*\s*)$/.exec(text);
  return match === null ? text : text.slice(0, match.index) + (match[1] ?? '');
}

// ---------------------------------------------------------------------------
// Image layout hints
// ---------------------------------------------------------------------------

export type ImageAlign = 'left' | 'center' | 'right';

export interface ImageHints {
  /** Requested width in CSS pixels; height follows to preserve aspect ratio. */
  width: number | null;
  align: ImageAlign | null;
}

/**
 * Reads MCDF layout hints out of an image title:
 * `![alt](assets/x.png "width=600 align=center")`.
 *
 * The title slot is used because it is the only place CommonMark already lets an
 * image carry extra text — a document with hints renders correctly in any
 * Markdown tool, which would not be true of a `{...}` attribute block. A title
 * with no recognised hint stays an ordinary title.
 */
export function parseImageHints(title: string): ImageHints {
  // Deliberately literal `width=` / `align=` with no spaces around `=`: Studio
  // already ships this convention (a plain substring search in imgui_md), and an
  // implementation that quietly accepted more would render documents the others
  // could not.
  const width = /width=(\d+(?:\.\d+)?)/i.exec(title);
  const align = /align=(left|center|right)/i.exec(title);
  const px = width === null ? NaN : Number.parseFloat(width[1] as string);
  return {
    width: Number.isFinite(px) && px > 0 ? px : null,
    align: align === null ? null : (align[1] as string).toLowerCase() as ImageAlign,
  };
}

export function hasImageHints(hints: ImageHints): boolean {
  return hints.width !== null || hints.align !== null;
}

/** Inline CSS implementing the hints, matching Studio's layout rules. */
export function imageHintStyle(hints: ImageHints): string {
  const rules: string[] = [];
  if (hints.width !== null) {
    // Studio scales an over-wide image down to the content region; max-width is
    // the CSS equivalent, and height:auto keeps the aspect ratio either way.
    rules.push(`width:${hints.width}px`, 'height:auto');
  }
  rules.push('max-width:100%');
  if (hints.align === 'center') {
    rules.push('display:block', 'margin-left:auto', 'margin-right:auto');
  } else if (hints.align === 'right') {
    rules.push('display:block', 'margin-left:auto', 'margin-right:0');
  } else if (hints.align === 'left') {
    rules.push('display:block', 'margin-left:0', 'margin-right:auto');
  }
  return rules.join(';');
}

const ENTITIES: Record<string, string> = {
  '&amp;': '&',
  '&lt;': '<',
  '&gt;': '>',
  '&quot;': '"',
};

function decodeAttribute(value: string): string {
  return value.replace(/&(?:amp|lt|gt|quot);/g, (m) => ENTITIES[m] ?? m);
}

/**
 * Applies layout hints to the `<img>` tags micromark produced.
 *
 * This rewrites *our own* renderer's output, one line after producing it —
 * micromark escapes `&`, `<`, `>` and `"` inside attribute values, so no
 * attribute can contain `>` and `<img[^>]*>` cannot over-match. It is not a
 * general-purpose HTML rewriter and must not be pointed at arbitrary input.
 */
function applyImageHints(html: string): string {
  return html.replace(/<img\s([^>]*)>/g, (tag, attrs: string) => {
    const title = /\stitle="([^"]*)"/.exec(attrs);
    if (title === null) return tag;

    const hints = parseImageHints(decodeAttribute(title[1] as string));
    if (!hasImageHints(hints)) return tag;

    // The hints are layout instructions, not prose: showing them as a tooltip
    // (which is what a title attribute does) would be noise. Studio suppresses
    // the tooltip for the same reason.
    //
    // The trailing `/` of the self-closing tag is part of the captured
    // attributes and has to come off before anything is appended, or the result
    // is `<img src="x" / width="600">` — which browsers forgive, so the preview
    // looked right for as long as the tests only searched for substrings.
    const rest = attrs
      .replace(/\stitle="[^"]*"/, '')
      .replace(/\s*\/\s*$/, '')
      .trim();
    const width = hints.width === null ? '' : ` width="${hints.width}"`;
    return `<img ${rest}${width} style="${imageHintStyle(hints)}" />`;
  });
}

/**
 * Options that make micromark agree with the reference implementation's md4c.
 *
 * Disabling the two raw-HTML constructs is not the same as escaping their
 * output. Escaping keeps `<div>x</div>` a *block*, which micromark then emits
 * unwrapped; disabling makes those bytes ordinary text, so they become a
 * paragraph like any other prose — which is md4c's `MD_FLAG_NOHTML` behaviour
 * and the rule §10.4 states: MCDF has no raw HTML, only characters that look
 * like it. Both readings are safe; only one is byte-identical.
 */
const NO_RAW_HTML = { disable: { null: ['htmlFlow', 'htmlText'] } };

function attrEscape(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/**
 * Turns `# Overview {#overview}` into `<h1 id="overview">Overview</h1>`.
 *
 * Canonical render 1 dropped the anchor, which left a rendered document unable
 * to express its own structure: the anchors are what bind schema sections to
 * headings (§4.2), so a reader could not link to the section a validator had
 * just checked. Render 2 carries them.
 *
 * The ids come from `parseHeadings` — the real parser, in document order, which
 * is the order `<hN>` tags appear in the output — and never from scanning the
 * source for lines starting with `#`.
 */
function applyHeadingIds(html: string, headings: Heading[]): string {
  let next = 0;
  return html.replace(/<h([1-6])>([\s\S]*?)<\/h\1>/g, (_all, level: string, inner: string) => {
    const id = headings[next]?.id ?? '';
    next++;
    if (id === '') return `<h${level}>${inner}</h${level}>`;
    const escaped = attrEscape(id);
    // The anchor reached the output as ordinary text; it is structure, not
    // prose, and it has just become the id attribute.
    return `<h${level} id="${escaped}">${stripTrailingAnchor(inner, escaped)}</h${level}>`;
  });
}

/**
 * Renders CommonMark to the canonical HTML body (spec §10.4) — the same bytes
 * `libmcdf` emits, and what the SPA also shows as its preview.
 *
 * Raw HTML and dangerous URL protocols stay disabled, so the result is safe to
 * inject into the page: untrusted document content cannot smuggle in script.
 */
export function renderHtml(markdown: string): string {
  return applyHeadingIds(
    applyImageHints(micromark(markdown, { extensions: [NO_RAW_HTML as never] })),
    parseHeadings(markdown),
  );
}

// ---------------------------------------------------------------------------
// Plain text
// ---------------------------------------------------------------------------

/** Block-level tokens whose end closes a paragraph of output text. */
const TEXT_BLOCK_ENDS = [
  'paragraph',
  'atxHeading',
  'setextHeading',
  'codeFenced',
  'codeIndented',
] as const;

/** Tokens whose source text is taken verbatim into the current block. */
const TEXT_CONTENT = [
  'data',
  'characterEscapeValue',
  'codeTextData',
  'codeFlowValue',
  // An autolink's URL is its own token rather than data, but it is prose the
  // reader sees — md4c reports it as ordinary text inside the link span.
  'autolinkProtocol',
  'autolinkEmail',
  // Character references are *not* decoded, matching the reference renderer.
  // Decoding would oblige every implementation to carry the HTML5 named-entity
  // table — some two thousand entries — to produce plain text, which is a steep
  // price for `&copy;`. The HTML render decodes them because its CommonMark
  // library already must.
  'characterReference',
] as const;

/**
 * Renders CommonMark to deterministic plain text (spec §10.4).
 *
 * Each paragraph, heading, list item and code block becomes one block; blocks
 * are trimmed, empties dropped, and the rest joined by a blank line. Soft line
 * breaks become a space and hard breaks a newline — the same block segmentation
 * `markdown_to_text` performs over md4c's callbacks, expressed here over
 * micromark's token stream.
 */
export function renderText(markdown: string): string {
  const blocks: string[] = [];
  let current: string[] = [];
  let depth = 0;
  let inCode = 0;
  // A URL is tokenized as data too, so `[link](x.md)` would otherwise read as
  // "linkx.md". The reference renderer never sees these at all — md4c reports a
  // destination as span *detail*, not as text — so suppressing them is what
  // agreeing with it means. Same for a fence's info string: ```py is syntax.
  let suppressed = 0;
  // A hard break is a token of its own followed by the line ending it consumed;
  // without this the newline it stands for would be doubled by a space.
  let breakPending = false;

  // Set while a heading block is open, so its anchor can be dropped on the way
  // out. The anchor is structure and never prose in either rendering: the HTML
  // turns it into an id, and plain text has nowhere to put it.
  let headingOpen = false;

  const flush = (): void => {
    let text = current.join('').trim();
    if (headingOpen) {
      const { text: stripped, id } = splitHeadingId(text);
      if (id !== '') text = stripped;
    }
    if (text !== '') blocks.push(text);
    current = [];
  };

  const enter: Record<string, () => void> = {};
  const exit: Record<string, (this: CompileContext, token: unknown) => void> = {};

  for (const name of TEXT_BLOCK_ENDS) {
    const isCode = name === 'codeFenced' || name === 'codeIndented';
    const isHeading = name === 'atxHeading' || name === 'setextHeading';
    enter[name] = (): void => {
      depth++;
      if (isCode) inCode++;
      if (isHeading) headingOpen = true;
    };
    exit[name] = (): void => {
      depth--;
      if (isCode) inCode--;
      flush();
      if (isHeading) headingOpen = false;
    };
  }
  for (const name of ['resource', 'reference', 'codeFencedFence']) {
    enter[name] = (): void => {
      suppressed++;
    };
    exit[name] = (): void => {
      if (suppressed > 0) suppressed--;
    };
  }
  for (const name of TEXT_CONTENT) {
    exit[name] = function (token): void {
      if (depth > 0 && suppressed === 0) current.push(this.sliceSerialize(token));
      breakPending = false;
    };
  }
  for (const name of ['hardBreakEscape', 'hardBreakTrailing']) {
    exit[name] = (): void => {
      if (depth > 0) current.push('\n');
      breakPending = true;
    };
  }
  exit['lineEnding'] = (): void => {
    if (depth === 0 || suppressed > 0) return;
    if (breakPending) {
      breakPending = false;
      return;
    }
    // Inside a code block the newline is content; in prose it is a soft break,
    // which flows into a single space.
    current.push(inCode > 0 ? '\n' : ' ');
  };

  micromark(markdown, {
    extensions: [NO_RAW_HTML as never],
    // The extension replaces the default handlers, so the compiled HTML is
    // intentionally discarded; the text is built in `blocks`.
    htmlExtensions: [{ enter, exit } as never],
  });
  flush();

  return blocks.join('\n\n');
}
