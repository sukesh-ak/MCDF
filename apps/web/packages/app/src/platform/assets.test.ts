// @vitest-environment jsdom
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { renderHtml } from 'mcdf-ts';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import { AssetUrlCache, resolveAssetUrls, toMemberPath } from './assets.ts';

const bytes = (s: string) => new TextEncoder().encode(s);

function sourceOf(members: Record<string, string>) {
  return {
    read: (path: string) => (path in members ? bytes(members[path] as string) : null),
    keyFor: (path: string) => members[path] ?? '',
  };
}

beforeEach(() => {
  let n = 0;
  vi.stubGlobal('URL', {
    ...URL,
    createObjectURL: vi.fn(() => `blob:mock/${++n}`),
    revokeObjectURL: vi.fn(),
  });
});

describe('toMemberPath', () => {
  it('normalises markdown destinations to container paths', () => {
    expect(toMemberPath('assets/w1-1.jpg')).toBe('assets/w1-1.jpg');
    expect(toMemberPath('./assets/w1.jpg')).toBe('assets/w1.jpg');
    expect(toMemberPath('/assets/w1.jpg')).toBe('assets/w1.jpg');
    expect(toMemberPath('assets\\w1.jpg')).toBe('assets/w1.jpg');
    expect(toMemberPath('assets/my%20logo.png')).toBe('assets/my logo.png');
    expect(toMemberPath('assets/w1.jpg#frag')).toBe('assets/w1.jpg');
  });

  it('survives a malformed percent-escape instead of dropping the reference', () => {
    expect(toMemberPath('assets/100%.png')).toBe('assets/100%.png');
  });
});

describe('resolveAssetUrls', () => {
  it('rewrites a container-relative image to a blob URL', () => {
    const cache = new AssetUrlCache();
    const html = renderHtml('![w1](assets/w1-1.jpg "width=600 align=center")\n');
    // The bug this fixes: the browser would resolve this against the page origin.
    expect(html).toContain('src="assets/w1-1.jpg"');

    const out = resolveAssetUrls(html, sourceOf({ 'assets/w1-1.jpg': 'JPEGBYTES' }), cache);
    expect(out.html).toContain('src="blob:mock/1"');
    expect(out.html).not.toContain('src="assets/w1-1.jpg"');
    expect(out.missing).toEqual([]);
  });

  it('marks a referenced asset that is not in the container', () => {
    const cache = new AssetUrlCache();
    const out = resolveAssetUrls(renderHtml('![w1](assets/gone.jpg)\n'), sourceOf({}), cache);
    expect(out.missing).toEqual(['assets/gone.jpg']);
    expect(out.html).toContain('Missing asset: assets/gone.jpg');
    expect(out.html).not.toContain('<img');
  });

  it('blocks external images instead of fetching them', () => {
    // Spec §4.1: renderers MUST NOT fetch external resources. Loading a remote
    // image would also tell that host the document is open.
    const cache = new AssetUrlCache();
    const out = resolveAssetUrls(
      renderHtml('![a](https://example.com/a.png)\n'),
      sourceOf({}),
      cache,
    );
    expect(out.blocked).toEqual(['https://example.com/a.png']);
    expect(out.html).toContain('External image blocked');

    // The critical assertion: no element remains that would issue a request.
    const doc = new DOMParser().parseFromString(out.html, 'text/html');
    expect(doc.querySelectorAll('img')).toHaveLength(0);
  });

  it('inherits micromark stripping non-http image protocols before we ever see them', () => {
    // micromark's image protocol allow-list is http/https, so `data:` and
    // `javascript:` sources are emptied at render time. Pinned here because the
    // resolver deliberately does not re-check what it is handed: if this ever
    // changes upstream, the assumption should fail loudly rather than silently
    // pass a hostile URL through.
    const cache = new AssetUrlCache();
    const md = '![b](data:image/gif;base64,AAA)\n\n![c](javascript:alert(1))\n';
    const out = resolveAssetUrls(renderHtml(md), sourceOf({}), cache);
    expect(out.html).not.toContain('data:image/gif');
    expect(out.html).not.toContain('javascript:');
    expect(out.missing).toEqual([]);
  });

  it('reuses one blob URL across re-renders and mints a new one when bytes change', () => {
    const cache = new AssetUrlCache();
    const html = renderHtml('![w1](assets/w1.png)\n');

    const first = resolveAssetUrls(html, sourceOf({ 'assets/w1.png': 'v1' }), cache);
    const again = resolveAssetUrls(html, sourceOf({ 'assets/w1.png': 'v1' }), cache);
    expect(again.html).toBe(first.html); // typing must not leak a URL per keystroke

    const edited = resolveAssetUrls(html, sourceOf({ 'assets/w1.png': 'v2' }), cache);
    expect(edited.html).not.toBe(first.html);
    expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:mock/1');
  });

  it('releases URLs for assets the document stops referencing', () => {
    const cache = new AssetUrlCache();
    const members = { 'assets/w1.png': 'v1' };
    resolveAssetUrls(renderHtml('![w1](assets/w1.png)\n'), sourceOf(members), cache);
    resolveAssetUrls(renderHtml('no images now\n'), sourceOf(members), cache);
    expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:mock/1');
  });

  it('does not let a document smuggle in a live event handler', () => {
    const cache = new AssetUrlCache();
    const md = '<img src=x onerror="alert(1)">\n\n<script>alert(1)</script>\n';
    const out = resolveAssetUrls(renderHtml(md), sourceOf({}), cache);

    // The text survives — escaped. Asserting on the string would pass on the
    // escaped form and on a live element alike, so parse it and check that no
    // executable element or handler actually exists.
    const doc = new DOMParser().parseFromString(out.html, 'text/html');
    expect(doc.querySelectorAll('script')).toHaveLength(0);
    expect(doc.querySelectorAll('[onerror]')).toHaveLength(0);
    expect(out.html).toContain('&lt;img'); // escaped, i.e. inert text
  });
});
