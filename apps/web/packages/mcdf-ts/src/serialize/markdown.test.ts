// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import {
  parseHeadings,
  parseImageHints,
  renderHtml,
  renderText,
  setHeadingAnchor,
  splitHeadingId,
} from './markdown.js';

describe('splitHeadingId', () => {
  it('extracts the anchor', () => {
    expect(splitHeadingId('Contract Overview {#overview}')).toEqual({
      text: 'Contract Overview',
      id: 'overview',
    });
  });

  it('leaves a plain heading alone', () => {
    expect(splitHeadingId('  Just a title  ')).toEqual({ text: 'Just a title', id: '' });
  });
});

describe('parseHeadings', () => {
  it('reads ATX headings with levels and anchors', () => {
    const headings = parseHeadings('# One {#a}\n\ntext\n\n### Three {#b}\n');
    expect(headings).toEqual([
      { level: 1, text: 'One', id: 'a', line: 1 },
      { level: 3, text: 'Three', id: 'b', line: 5 },
    ]);
  });

  it('handles a closing sequence without inflating the level', () => {
    expect(parseHeadings('## Two ##\n')).toEqual([{ level: 2, text: 'Two', id: '', line: 1 }]);
  });

  it('reads setext headings', () => {
    expect(parseHeadings('Title {#t}\n=====\n\nSub\n---\n')).toEqual([
      { level: 1, text: 'Title', id: 't', line: 1 },
      { level: 2, text: 'Sub', id: '', line: 4 },
    ]);
  });

  it('strips inline markup from heading text, as md4c does', () => {
    expect(parseHeadings('# A *bold* `run` {#x}\n')).toEqual([
      { level: 1, text: 'A bold run', id: 'x', line: 1 },
    ]);
  });

  it('keeps link and image URLs out of the heading text', () => {
    expect(parseHeadings('# [Overview](https://example.com) {#overview}\n')).toEqual([
      { level: 1, text: 'Overview', id: 'overview', line: 1 },
    ]);
    expect(parseHeadings('# See [it][ref] {#see}\n\n[ref]: https://example.com\n')).toEqual([
      { level: 1, text: 'See it', id: 'see', line: 1 },
    ]);
    expect(parseHeadings('# ![logo](assets/logo.png "a title") Brand {#brand}\n')).toEqual([
      { level: 1, text: 'logo Brand', id: 'brand', line: 1 },
    ]);
  });

  it('finds anchors in a document that opens with an image', () => {
    const md = '![w1](assets/w1-1.jpg "width=600 align=center")\n\n# Overview {#overview}\n';
    expect(parseHeadings(md)).toEqual([{ level: 1, text: 'Overview', id: 'overview', line: 3 }]);
  });

  it('ignores hashes inside fenced code blocks', () => {
    expect(parseHeadings('```\n# not a heading\n```\n')).toEqual([]);
  });

  it('ignores hashes inside indented code blocks', () => {
    expect(parseHeadings('    # not a heading\n')).toEqual([]);
  });

  it('returns nothing for an empty document', () => {
    expect(parseHeadings('')).toEqual([]);
  });
});

describe('setHeadingAnchor', () => {
  const doc = '# Title\n\nBody.\n\n## Status\n\nMore.\n';

  it('adds an anchor to the heading on the given line', () => {
    expect(setHeadingAnchor(doc, 5, 'status')).toContain('## Status {#status}');
    expect(parseHeadings(setHeadingAnchor(doc, 5, 'status'))[1]).toEqual({
      level: 2,
      text: 'Status',
      id: 'status',
      line: 5,
    });
  });

  it('replaces an anchor the heading already has', () => {
    const once = setHeadingAnchor(doc, 1, 'a');
    expect(setHeadingAnchor(once, 1, 'b')).toContain('# Title {#b}');
    expect(setHeadingAnchor(once, 1, 'b')).not.toContain('{#a}');
  });

  it('drops a cosmetic closing ## run', () => {
    // The run is decoration, and writing `## Two {#two} ##` puts the anchor
    // somewhere every tool has to reason about. Removing it leaves one obvious
    // form. (Rendering a closed heading is no longer a problem in itself —
    // render 2 reads anchors from the parse, so `### Deep {#d} ###` renders
    // correctly either way.)
    const out = setHeadingAnchor('## Two ##\n', 1, 'two');
    expect(out).toBe('## Two {#two}\n');
    expect(parseHeadings(out)).toEqual([{ level: 2, text: 'Two', id: 'two', line: 1 }]);
    expect(renderHtml(out)).toContain('<h2 id="two">Two</h2>');
  });

  it('leaves everything else on the line alone', () => {
    expect(setHeadingAnchor(doc, 5, 'status').split('\n')[4]).toBe('## Status {#status}');
    expect(setHeadingAnchor(doc, 5, 'status').split('\n').length).toBe(doc.split('\n').length);
  });

  it('refuses a line that is out of range, blank, or a setext underline', () => {
    expect(setHeadingAnchor(doc, 0, 'x')).toBe(doc);
    expect(setHeadingAnchor(doc, 99, 'x')).toBe(doc);
    expect(setHeadingAnchor(doc, 2, 'x')).toBe(doc); // blank line
    expect(setHeadingAnchor('Title\n=====\n', 2, 'x')).toBe('Title\n=====\n');
  });

  it('anchors a setext heading on its text line', () => {
    const out = setHeadingAnchor('Title\n=====\n', 1, 't');
    expect(out).toBe('Title {#t}\n=====\n');
    expect(parseHeadings(out)[0]?.id).toBe('t');
  });
});

describe('parseImageHints', () => {
  it('reads width and align in either order, case-insensitively', () => {
    expect(parseImageHints('width=600 align=center')).toEqual({ width: 600, align: 'center' });
    expect(parseImageHints('align=RIGHT width=42.5')).toEqual({ width: 42.5, align: 'right' });
  });

  it('requires the literal form Studio accepts, with no spaces around =', () => {
    expect(parseImageHints('width = 600')).toEqual({ width: null, align: null });
  });

  it('returns nothing for a plain caption', () => {
    expect(parseImageHints('A photo of the site')).toEqual({ width: null, align: null });
    expect(parseImageHints('')).toEqual({ width: null, align: null });
  });

  it('ignores a nonsensical width rather than emitting a broken style', () => {
    expect(parseImageHints('width=0').width).toBeNull();
    expect(parseImageHints('width=abc').width).toBeNull();
    expect(parseImageHints('width=-5').width).toBeNull();
  });

  it('ignores an unknown alignment', () => {
    expect(parseImageHints('align=middle').align).toBeNull();
  });
});

describe('heading anchors', () => {
  it('becomes an id attribute, and leaves the heading text clean', () => {
    expect(renderHtml('# Overview {#overview}\n')).toBe(
      '<h1 id="overview">Overview</h1>\n',
    );
  });

  it('generates no id for a heading without an anchor', () => {
    // Slugifying the text is what most renderers do, and no two agree on how, so
    // the canonical render generates nothing (spec §10.4).
    expect(renderHtml('# Plain Heading\n')).toBe('<h1>Plain Heading</h1>\n');
  });

  // The cases a line scan gets wrong. Anchors come from the parse, so a heading
  // is a heading wherever it sits, and a '#' that is not one is left alone.
  it('is read from the parse, not from lines that start with #', () => {
    // A code sample is content: the renderer must not edit it.
    expect(renderHtml('```\n# Fake {#nope}\n```\n')).toContain('# Fake {#nope}');
    expect(renderHtml('```\n# Fake {#nope}\n```\n')).not.toContain('id=');
    // Headings that do not begin their line, and setext headings.
    expect(renderHtml('> ## Quoted {#q}\n')).toContain('<h2 id="q">Quoted</h2>');
    expect(renderHtml('Title {#sx}\n=====\n')).toContain('<h1 id="sx">Title</h1>');
    // A closed ATX heading: the old line scan refused this one because the line
    // does not end in `}`, so the anchor stayed visible while the schema bound it.
    expect(renderHtml('### Deep {#d} ###\n')).toBe('<h3 id="d">Deep</h3>\n');
    expect(parseHeadings('### Deep {#d} ###\n')).toEqual([
      { level: 3, text: 'Deep', id: 'd', line: 1 },
    ]);
  });

  it('attaches each anchor to its own heading', () => {
    const html = renderHtml('# One {#a}\n\n## Two\n\n### Three {#c}\n');
    expect(html).toBe('<h1 id="a">One</h1>\n<h2>Two</h2>\n<h3 id="c">Three</h3>\n');
  });

  it('drops the anchor from plain text, which has nowhere to put it', () => {
    expect(renderText('> ## Quoted {#q}\n')).toBe('Quoted');
    expect(renderText('Title {#sx}\n=====\n')).toBe('Title');
    expect(renderText('```\n# Fake {#nope}\n```\n')).toBe('# Fake {#nope}');
  });
});

describe('renderHtml', () => {
  it('renders CommonMark', () => {
    expect(renderHtml('# Hi\n')).toContain('<h1>Hi</h1>');
  });

  it('escapes raw HTML rather than passing it through', () => {
    const html = renderHtml('<script>alert(1)</script>\n');
    expect(html).not.toContain('<script>');
    expect(html).toContain('&lt;script&gt;');
  });

  it('applies width and alignment hints from the image title', () => {
    // Asserted as the whole tag, not as substrings. Substring assertions passed
    // for months against `<img src="…" alt="w1" / width="600" …>` — the trailing
    // slash of the self-closing tag was still in the attributes when the hint
    // attributes were appended, which browsers forgive and a byte comparison
    // against the reference implementation does not.
    expect(renderHtml('![w1](assets/w1.jpg "width=600 align=center")\n')).toBe(
      '<p><img src="assets/w1.jpg" alt="w1" width="600" ' +
        'style="width:600px;height:auto;max-width:100%;display:block;' +
        'margin-left:auto;margin-right:auto" /></p>\n',
    );
  });

  it('keeps a genuine title that carries no hints', () => {
    const html = renderHtml('![w1](assets/w1.jpg "A photo of the site")\n');
    expect(html).toContain('title="A photo of the site"');
    expect(html).not.toContain('style=');
  });

  it('accepts alignment without a width, and width without alignment', () => {
    expect(renderHtml('![a](a.png "align=right")\n')).toContain('margin-left:auto;margin-right:0');
    const sized = renderHtml('![a](a.png "width=320")\n');
    expect(sized).toContain('width="320"');
    expect(sized).not.toContain('display:block');
  });

  it('always caps an image at the container width, as Studio does', () => {
    expect(renderHtml('![a](a.png "width=99999")\n')).toContain('max-width:100%');
  });

  it('drops dangerous link protocols', () => {
    // eslint-disable-next-line no-script-url
    const html = renderHtml('[x](javascript:alert(1))\n');
    expect(html).not.toContain('javascript:');
  });

  // ---- canonical render, spec §10.4 ---------------------------------------
  //
  // Each of these is a place where micromark and the reference implementation's
  // md4c were observed to disagree. A regression is not cosmetic: it is this
  // implementation drifting away from every other one.

  it('self-closes void elements', () => {
    expect(renderHtml('a  \nb\n\n---\n')).toBe('<p>a<br />\nb</p>\n<hr />\n');
  });

  it('starts every block on its own line, including inside a list item', () => {
    expect(renderHtml('- a\n\n- b\n')).toContain('<li>\n<p>a</p>');
    expect(renderHtml('- one\n  - nested\n')).toContain('<li>one\n<ul>');
  });

  it('treats raw HTML as text rather than as a block', () => {
    // Escaping it while still recognising the block — micromark's default — is
    // equally safe and produces different bytes, so §10.4 picks one.
    expect(renderHtml('<div>x</div>\n')).toBe('<p>&lt;div&gt;x&lt;/div&gt;</p>\n');
  });

  it('decodes character references in HTML', () => {
    expect(renderHtml('&copy; &amp; done\n')).toBe('<p>© &amp; done</p>\n');
  });
});

describe('renderText', () => {
  it('joins blocks with a blank line and strips inline markup', () => {
    expect(renderText('# Title {#t}\n\nHello **world**.\n')).toBe('Title\n\nHello world.');
  });

  it('separates nested list items', () => {
    // The reference renderer flushed only when an item *closed*, so a nested
    // list appended into its parent's buffer and this came out "onenesteddeep".
    expect(renderText('- one\n  - nested\n    - deep\n')).toBe('one\n\nnested\n\ndeep');
  });

  it('folds soft breaks to a space and keeps hard breaks', () => {
    expect(renderText('soft\nbreak\n')).toBe('soft break');
    expect(renderText('hard  \nbreak\n')).toBe('hard\nbreak');
    expect(renderText('slash\\\nbreak\n')).toBe('slash\nbreak');
  });

  it('keeps prose and drops syntax', () => {
    const text = renderText('[label](https://example.org/page) and <https://example.org/auto>\n');
    expect(text).toContain('label');
    expect(text).toContain('https://example.org/auto'); // an autolink is prose
    expect(text).not.toContain('example.org/page'); // a destination is syntax
    expect(renderText('```py\nx = 1\n```\n')).toBe('x = 1'); // an info string is syntax
  });

  it('keeps newlines inside a code block', () => {
    expect(renderText('```\none\ntwo\n```\n')).toBe('one\ntwo');
  });

  it('leaves character references undecoded', () => {
    // Decoding would oblige every implementation to carry the HTML5 entity table
    // just to produce plain text (spec §10.4).
    expect(renderText('&copy; 2026\n')).toBe('&copy; 2026');
  });
});
