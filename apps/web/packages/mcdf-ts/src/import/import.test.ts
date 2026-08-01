// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// End-to-end import tests.
//
// The assertion that matters most in every one of these is the last:
// `validate(...).ok`. An importer that produces plausible Markdown but a
// container that does not pass its own conformance checks has not imported
// anything — and because these run against the real `validate`, a change that
// breaks the binding between generated anchors and the generated schema fails
// here rather than in a user's browser.

import { describe, expect, it } from 'vitest';

import { loadDocument } from '../core/document.js';
import { readAuditLog, auditVerify } from '../core/audit.js';
import { verifyManifest } from '../core/manifest.js';
import { renderHtml } from '../serialize/markdown.js';
import { validate } from '../core/validate.js';
import { MCDF_TS_VERSION } from '../version.js';
import { importEpub } from './epub.js';
import { importHtml } from './html.js';
import { importMarkdown } from './markdown.js';
import { makeZip, type ZipInput } from './zip.test.js';

const NOW = '2026-08-01T12:00:00Z';
const options = { now: NOW, actor: 'mcdf-web' };

const png = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

describe('importMarkdown', () => {
  it('wraps a Markdown file without converting anything', async () => {
    const source = '# Title {#intro}\n\nSome *text* with `code`.\n';
    const { container, report } = await importMarkdown(source, {
      ...options,
      source: 'notes.md',
    });

    expect(container.readText('content.md')).toBe(source);
    expect(report.title).toBe('Title');
    expect(report.format).toBe('markdown');
    // No schema: a plain Markdown file makes no structural claims, and
    // inventing sections would put text in the document nobody wrote.
    expect(container.has('schema.yaml')).toBe(false);
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);
  });

  it('stamps provenance and records an IMPORTED audit entry', async () => {
    const { container } = await importMarkdown('# T\n', options);

    const metadata = loadDocument(container).metadata;
    expect(metadata.generated_by).toBe(`mcdf-ts markdown import ${MCDF_TS_VERSION}`);

    const log = readAuditLog(container);
    expect(log).toHaveLength(1);
    expect(log[0]).toMatchObject({ action: 'IMPORTED', actor: 'mcdf-web', timestamp: NOW });
    expect(auditVerify(container).ok).toBe(true);
  });

  it('pulls referenced images into assets/ and rewrites the links', async () => {
    const { container, report } = await importMarkdown(
      '# T\n\n![a figure](pics/fig.png "width=600")\n',
      { ...options, resolveAsset: (path) => (path === 'pics/fig.png' ? png : null) },
    );

    expect(container.readText('content.md')).toContain('![a figure](assets/fig.png "width=600")');
    expect(container.read('assets/fig.png')).toEqual(png);
    expect(report.images).toBe(1);
  });

  it('reports an image it could not find instead of failing', async () => {
    const { container, report } = await importMarkdown('# T\n\n![x](pics/missing.png)\n', options);

    expect(report.images).toBe(0);
    expect(report.notes.join(' ')).toMatch(/could not be found/);
    // The link still points at the member that would have been there, so
    // attaching the file later repairs the document.
    expect(container.readText('content.md')).toContain('(assets/missing.png)');
  });

  it('leaves external images alone', async () => {
    const { container } = await importMarkdown(
      '# T\n\n![x](https://example.com/a.png)\n',
      options,
    );
    expect(container.readText('content.md')).toContain('https://example.com/a.png');
  });
});

describe('importHtml', () => {
  const page = `<!doctype html>
<html><head><title>Release Notes</title><style>b{color:red}</style></head>
<body>
  <h1>Release Notes</h1>
  <p>First <em>paragraph</em>.</p>
  <h2>Fixes</h2>
  <ul><li>One</li><li>Two</li></ul>
  <img src="img/shot.png" alt="a screenshot" width="640">
  <script>alert(1)</script>
</body></html>`;

  it('converts a page into a bound, valid document', async () => {
    const { container, report } = await importHtml(page, {
      ...options,
      source: 'notes.html',
      resolveAsset: () => png,
    });

    const content = container.readText('content.md');
    expect(content).toContain('# Release Notes {#document}');
    expect(content).toContain('First *paragraph*.');
    // Source headings demote so the importer's H1 is the only top-level one.
    expect(content).toContain('### Fixes');
    expect(content).not.toContain('## Release Notes');

    expect(report.title).toBe('Release Notes');
    expect(validate(container, loadDocument(container), 'integrity').ok).toBe(true);
  });

  it('produces Markdown that renders back to the structure it came from', async () => {
    // Asserted through the renderer rather than by matching strings, because
    // the question is whether the list is a list — not how turndown chose to
    // pad it. It emits `-   One`, which is deliberate: the 4-column indent is
    // what keeps a nested list nested, and is not worth overriding.
    const { container } = await importHtml(
      `<html><body><h1>T</h1><ul><li>One<ul><li>Nested</li></ul></li><li>Two</li></ul></body></html>`,
      options,
    );
    const html = renderHtml(container.readText('content.md'));
    expect(html).toMatch(/<ul>[\s\S]*<li>[\s\S]*One[\s\S]*<ul>[\s\S]*<li>[\s\S]*Nested/);
    expect(html).toContain('Two');
  });

  it('drops scripts and styles rather than carrying them into content.md', async () => {
    const { container } = await importHtml(page, { ...options, resolveAsset: () => png });
    const content = container.readText('content.md');
    expect(content).not.toContain('alert(1)');
    expect(content).not.toContain('color:red');
  });

  it('carries a pixel width into the layout-hint slot', async () => {
    const { container } = await importHtml(page, { ...options, resolveAsset: () => png });
    expect(container.readText('content.md')).toContain(
      '![a screenshot](assets/shot.png "width=640")',
    );
  });

  it('ignores a width that the hint syntax cannot express', async () => {
    const { container } = await importHtml(
      '<html><body><img src="a.png" alt="x" width="50%"></body></html>',
      { ...options, resolveAsset: () => png },
    );
    expect(container.readText('content.md')).toContain('![x](assets/a.png)');
  });

  it('names the document by its first heading when there is no title', async () => {
    const { report, container } = await importHtml(
      '<html><body><h1>Only Heading</h1><p>Body.</p></body></html>',
      options,
    );
    expect(report.title).toBe('Only Heading');
    // Not repeated: the source heading and the emitted H1 are the same heading.
    expect(container.readText('content.md')).toBe(
      '# Only Heading {#document}\n\nBody.\n',
    );
  });

  it('keeps a regular table as a pipe table and flattens an irregular one', async () => {
    const { container, report } = await importHtml(
      `<html><body>
        <table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>
        <table><tr><td colspan="2">merged</td></tr><tr><td>a</td><td>b</td></tr></table>
      </body></html>`,
      options,
    );
    const content = container.readText('content.md');
    expect(content).toContain('| A | B |');
    expect(content).toContain('| 1 | 2 |');
    expect(content).toContain('merged');
    expect(report.notes.join(' ')).toMatch(/flattened to plain text/);
  });

  it('refuses a page with nothing in it', async () => {
    await expect(importHtml('<html><body></body></html>', options)).rejects.toThrow(
      /no readable content/,
    );
  });
});

describe('importEpub', () => {
  const opf = `<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>A Short Book</dc:title>
    <dc:creator>Ada Lovelace</dc:creator>
    <dc:creator>Charles Babbage</dc:creator>
    <dc:language>en</dc:language>
    <dc:date>1843-08-01</dc:date>
  </metadata>
  <manifest>
    <item id="c1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
    <item id="css" href="style.css" media-type="text/css"/>
    <item id="fig" href="images/fig.png" media-type="image/png"/>
  </manifest>
  <spine>
    <itemref idref="c2"/>
    <itemref idref="c1"/>
    <itemref idref="css"/>
  </spine>
</package>`;

  const chapter = (title: string, body: string) =>
    `<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>${title}</title></head><body><h1>${title}</h1>${body}</body></html>`;

  const book = (extra: ZipInput[] = []): Promise<Uint8Array> =>
    makeZip([
      { name: 'mimetype', data: 'application/epub+zip', stored: true },
      {
        name: 'META-INF/container.xml',
        data: `<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>`,
      },
      { name: 'OEBPS/content.opf', data: opf },
      {
        name: 'OEBPS/text/ch1.xhtml',
        data: chapter('The Beginning', '<p>It was a <em>dark</em> night.</p>'),
      },
      {
        name: 'OEBPS/text/ch2.xhtml',
        data: chapter('A Preface', '<p>Before we start.</p><img src="../images/fig.png" alt="a figure"/>'),
      },
      { name: 'OEBPS/images/fig.png', data: png },
      { name: 'OEBPS/style.css', data: 'body { color: red }' },
      ...extra,
    ]);

  it('follows the spine, not the manifest order', async () => {
    const { container, report } = await importEpub(await book(), {
      ...options,
      source: 'book.epub',
    });
    const content = container.readText('content.md');

    // The spine lists ch2 first; reading order is the whole point.
    expect(content.indexOf('# A Preface')).toBeLessThan(content.indexOf('# The Beginning'));
    expect(report.chapters).toBe(2);
  });

  it('binds every generated section to a generated anchor', async () => {
    const { container } = await importEpub(await book(), options);
    const doc = loadDocument(container);

    expect(doc.schema.document_type).toBe('book');
    expect(doc.schema.sections.map((s) => s.id)).toEqual(['a-preface', 'the-beginning']);
    // Structure out of structure: the anchors and the schema are generated
    // together, so a fresh import is Core-clean without the user fixing it.
    expect(validate(container, doc, 'integrity').ok).toBe(true);
    expect(verifyManifest(container, doc.manifest).ok).toBe(true);
  });

  it('carries Dublin Core metadata across', async () => {
    const { container, report } = await importEpub(await book(), options);
    const metadata = loadDocument(container).metadata;

    expect(metadata.title).toBe('A Short Book');
    expect(metadata.authors.map((a) => a.name)).toEqual(['Ada Lovelace', 'Charles Babbage']);
    expect(metadata.language).toBe('en');
    expect(metadata.created_at).toBe('1843-08-01');
    expect(metadata.generated_by).toBe(`mcdf-ts epub import ${MCDF_TS_VERSION}`);
    expect(report.authors).toEqual(['Ada Lovelace', 'Charles Babbage']);
  });

  it('extracts images and rewrites their links', async () => {
    const { container, report } = await importEpub(await book(), options);
    expect(container.read('assets/fig.png')).toEqual(png);
    expect(container.readText('content.md')).toContain('![a figure](assets/fig.png)');
    expect(report.images).toBe(1);
  });

  it('leaves non-document spine entries out', async () => {
    const { container } = await importEpub(await book(), options);
    expect(container.readText('content.md')).not.toContain('color: red');
    expect(container.list()).not.toContain('assets/style.css');
  });

  it('reports what it could not carry over', async () => {
    const { report } = await importEpub(await book(), options);
    expect(report.notes.join(' ')).toMatch(/styling \(CSS\), fonts and page layout/);
    expect(report.notes.join(' ')).toMatch(/cross-references/);
  });

  it('refuses an archive that is not an EPUB', async () => {
    const notABook = await makeZip([{ name: 'readme.txt', data: 'hello' }]);
    await expect(importEpub(notABook, options)).rejects.toThrow(/container\.xml is missing/);
  });

  it('refuses an EPUB whose spine has no readable documents', async () => {
    const empty = await makeZip([
      {
        name: 'META-INF/container.xml',
        data: `<container><rootfiles><rootfile full-path="c.opf"/></rootfiles></container>`,
      },
      { name: 'c.opf', data: '<package><manifest/><spine/></package>' },
    ]);
    await expect(importEpub(empty, options)).rejects.toThrow(/no readable documents/);
  });

  it('does not follow a chapter reference that escapes the archive', async () => {
    const hostile = await makeZip([
      {
        name: 'META-INF/container.xml',
        data: `<container><rootfiles><rootfile full-path="c.opf"/></rootfiles></container>`,
      },
      {
        name: 'c.opf',
        data: `<package><metadata><dc:title>H</dc:title></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>`,
      },
      {
        name: 'a.xhtml',
        data: chapter('Ch', '<img src="../../../etc/passwd" alt="escape"/>'),
      },
    ]);

    const { container } = await importEpub(hostile, options);
    // Resolved to nothing, so it never became a member and never became a read.
    expect(container.list().some((p) => p.startsWith('assets/'))).toBe(false);
    expect(container.readText('content.md')).not.toContain('passwd');
  });

  it('resolves chapters against a percent-encoded package path', async () => {
    // The package sits in a directory whose name needs encoding in the href.
    // Deriving the base directory from the raw form would send every chapter
    // and image lookup somewhere that does not exist.
    const encoded = await makeZip([
      {
        name: 'META-INF/container.xml',
        data: `<container><rootfiles><rootfile full-path="My%20Book/content.opf"/></rootfiles></container>`,
      },
      {
        name: 'My Book/content.opf',
        data: `<package><metadata><dc:title>Spaced</dc:title></metadata><manifest><item id="a" href="text/ch.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>`,
      },
      { name: 'My Book/text/ch.xhtml', data: chapter('Only', '<p>Found it.</p>') },
    ]);

    const { container, report } = await importEpub(encoded, options);
    expect(report.chapters).toBe(1);
    expect(container.readText('content.md')).toContain('Found it.');
  });

  it('gives an untitled chapter a name and a unique anchor', async () => {
    const untitled = await makeZip([
      {
        name: 'META-INF/container.xml',
        data: `<container><rootfiles><rootfile full-path="c.opf"/></rootfiles></container>`,
      },
      {
        name: 'c.opf',
        data: `<package><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>`,
      },
      { name: 'a.xhtml', data: '<html><body><p>Body one.</p></body></html>' },
      { name: 'b.xhtml', data: '<html><body><p>Body two.</p></body></html>' },
    ]);

    const { container } = await importEpub(untitled, options);
    const doc = loadDocument(container);
    expect(doc.schema.sections.map((s) => s.id)).toEqual(['chapter-1', 'chapter-2']);
    expect(validate(container, doc, 'integrity').ok).toBe(true);
  });
});
