// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { MemoryContainer } from '../container/container.js';
import { buildManifest, manifestToCanonicalJson } from './manifest.js';
import { encryptContainer } from './encrypt.js';
import { EncPrivateKey } from '../crypto/enc-keys.js';
import { renderCanonical, CANONICAL_RENDER_VERSION, RenderError } from './render.js';

function document(content: string, extras: Record<string, string> = {}): MemoryContainer {
  const c = new MemoryContainer();
  c.writeText('content.md', content);
  for (const [path, text] of Object.entries(extras)) c.writeText(path, text);
  return c;
}

function withManifest(c: MemoryContainer): MemoryContainer {
  c.writeText('manifest.json', `${manifestToCanonicalJson(buildManifest(c))}\n`);
  return c;
}

describe('renderCanonical html', () => {
  it('emits a self-contained document with a strict CSP', () => {
    const html = renderCanonical(document('# Hi\n'), 'html');
    expect(html.startsWith('<!DOCTYPE html>\n<html lang="en">\n')).toBe(true);
    expect(html).toContain("default-src 'none'");
    expect(html).toContain('<h1>Hi</h1>');
    expect(html.endsWith('</body>\n</html>\n')).toBe(true);
  });

  it('names the render format, never this implementation', () => {
    // A renderer that stamps its own version puts byte-parity out of reach of
    // every other implementation and makes the published vectors go stale at
    // each release — which is exactly what the reference used to do.
    const html = renderCanonical(document('x\n'), 'html');
    expect(html).toContain(`<meta name="generator" content="mcdf-render/${CANONICAL_RENDER_VERSION}">`);
    expect(html).not.toContain('mcdf-ts');
  });

  it('escapes the metadata title and falls back when there is none', () => {
    expect(renderCanonical(document('x\n', { 'metadata.yaml': 'title: "A & B <c>"\n' }), 'html')).toContain(
      '<title>A &amp; B &lt;c&gt;</title>',
    );
    expect(renderCanonical(document('x\n'), 'html')).toContain('<title>MCDF Document</title>');
  });

  it('stamps the canonical manifest hash, and omits the line without a manifest', () => {
    expect(renderCanonical(withManifest(document('x\n')), 'html')).toContain(
      '<meta name="mcdf-source" content="sha256:',
    );
    expect(renderCanonical(document('x\n'), 'html')).not.toContain('mcdf-source');
  });

  it('is deterministic', () => {
    const c = withManifest(document('# T\n\nBody.\n'));
    expect(renderCanonical(c, 'html')).toBe(renderCanonical(c, 'html'));
  });
});

describe('renderCanonical text', () => {
  it('ends with exactly one newline', () => {
    expect(renderCanonical(document('# T\n\nBody.\n'), 'text')).toBe('T\n\nBody.\n');
  });
});

describe('renderCanonical refusals', () => {
  it('reports a missing content.md rather than rendering nothing', () => {
    const c = new MemoryContainer();
    c.writeText('metadata.yaml', 'title: Empty\n');
    expect(() => renderCanonical(c, 'html')).toThrow(/content.md is missing/);
  });

  it('refuses to render sealed content', async () => {
    // A CommonMark parser handed ciphertext does not fail — it produces
    // confident nonsense with headings nobody wrote (spec §6, §11).
    const key = EncPrivateKey.generate();
    const c = withManifest(document('# Secret {#s}\n'));
    await encryptContainer(c, ['content.md'], [key.publicKey()]);

    try {
      renderCanonical(c, 'html');
      expect.unreachable('rendering sealed content must fail');
    } catch (e) {
      expect((e as RenderError).code).toBe('E_CONTENT_SEALED');
    }
  });
});
