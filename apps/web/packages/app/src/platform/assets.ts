// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Resolves relative asset references in rendered markdown against the container.
//
// `![logo](assets/logo.png)` is a path into the *document*, not into the web
// origin. Left alone, the browser resolves it against the page URL and requests
// `/MCDF/assets/logo.png`, which does not exist — the image silently breaks.
// Studio has the same job and solves it by resolving against the container's
// working directory; the browser equivalent is a blob URL over the member bytes.
//
// The rewrite happens on an inert document from DOMParser, so no request is ever
// issued for the unresolved URL.

/** Supplies member bytes and a change key (the digest) for a container path. */
export interface AssetSource {
  read(path: string): Uint8Array | null;
  /** Changes when the member's bytes change, so stale blob URLs are replaced. */
  keyFor(path: string): string;
}

const MIME_BY_EXTENSION: Record<string, string> = {
  apng: 'image/apng',
  avif: 'image/avif',
  bmp: 'image/bmp',
  gif: 'image/gif',
  ico: 'image/x-icon',
  jpeg: 'image/jpeg',
  jpg: 'image/jpeg',
  png: 'image/png',
  svg: 'image/svg+xml',
  webp: 'image/webp',
};

function mimeFor(path: string): string {
  const ext = path.slice(path.lastIndexOf('.') + 1).toLowerCase();
  return MIME_BY_EXTENSION[ext] ?? 'application/octet-stream';
}

/** An absolute or protocol-relative URL — something outside the container. */
function isExternal(url: string): boolean {
  return /^[a-z][a-z0-9+.-]*:/i.test(url) || url.startsWith('//');
}

/** A URI that carries its own bytes; nothing is fetched to display it. */
function isSelfContained(url: string): boolean {
  return /^data:/i.test(url) || /^blob:/i.test(url);
}

/**
 * Normalises a markdown destination to a container member path: percent-decoded,
 * backslashes folded to `/`, and any `./` prefix removed. A leading `/` is
 * dropped too — inside a container it means the container root, not the origin.
 */
export function toMemberPath(url: string): string {
  let path = url;
  try {
    path = decodeURIComponent(path);
  } catch {
    // Malformed escapes: use the raw text rather than discarding the reference.
  }
  path = path.split('\\').join('/').replace(/^\.\//, '').replace(/^\/+/, '');
  const query = path.search(/[?#]/);
  return query === -1 ? path : path.slice(0, query);
}

/**
 * Blob URLs for container members, keyed by path and invalidated by digest.
 *
 * Blob URLs are documents in the browser's memory until revoked, so an editor
 * that re-renders on every keystroke must reuse them rather than mint a new one
 * per frame.
 */
export class AssetUrlCache {
  #entries = new Map<string, { key: string; url: string }>();

  urlFor(path: string, bytes: Uint8Array, key: string): string {
    const existing = this.#entries.get(path);
    if (existing !== undefined && existing.key === key) return existing.url;
    if (existing !== undefined) URL.revokeObjectURL(existing.url);

    const blob = new Blob([bytes as unknown as BlobPart], { type: mimeFor(path) });
    const url = URL.createObjectURL(blob);
    this.#entries.set(path, { key, url });
    return url;
  }

  /** Releases URLs for members no longer referenced by the document. */
  prune(live: Set<string>): void {
    for (const [path, entry] of this.#entries) {
      if (live.has(path)) continue;
      URL.revokeObjectURL(entry.url);
      this.#entries.delete(path);
    }
  }

  revokeAll(): void {
    for (const entry of this.#entries.values()) URL.revokeObjectURL(entry.url);
    this.#entries.clear();
  }
}

export interface ResolveResult {
  html: string;
  /** Referenced paths that are not members of the container. */
  missing: string[];
  /** External URLs that were blocked rather than fetched (spec §4.1). */
  blocked: string[];
}

/**
 * Rewrites container-relative `<img src>` to blob URLs, and replaces references
 * to absent members with a visible marker.
 *
 * A broken image in a document about *integrity* should say what is wrong rather
 * than render a blank box: a missing asset usually means the file was never
 * attached to the container.
 */
export function resolveAssetUrls(
  html: string,
  source: AssetSource,
  cache: AssetUrlCache,
): ResolveResult {
  if (typeof DOMParser === 'undefined') return { html, missing: [], blocked: [] };

  // DOMParser produces an inert document: nothing here triggers a network fetch,
  // so an external or unresolved URL is never requested even momentarily.
  const doc = new DOMParser().parseFromString(html, 'text/html');
  const used = new Set<string>();
  const missing: string[] = [];
  const blocked: string[] = [];

  const replaceWithMarker = (img: Element, text: string): void => {
    const marker = doc.createElement('span');
    marker.className = 'mcdf-missing-asset';
    marker.textContent = text;
    const alt = img.getAttribute('alt');
    if (alt !== null && alt !== '') marker.title = alt;
    // Layout hints belong to the image; carry them over so the placeholder sits
    // where the image would have.
    const style = img.getAttribute('style');
    if (style !== null) marker.setAttribute('style', style);
    img.replaceWith(marker);
  };

  for (const img of Array.from(doc.querySelectorAll('img'))) {
    const src = img.getAttribute('src');
    if (src === null || src === '') continue;
    if (isSelfContained(src)) continue;

    // Spec §4.1: "renderers MUST NOT fetch external resources". Rendering a
    // remote image would also leak to that host that this document is open,
    // which contradicts the promise that nothing leaves the tab.
    if (isExternal(src)) {
      if (!blocked.includes(src)) blocked.push(src);
      replaceWithMarker(img, `External image blocked: ${src}`);
      continue;
    }

    const path = toMemberPath(src);
    const bytes = source.read(path);
    if (bytes !== null) {
      img.setAttribute('src', cache.urlFor(path, bytes, source.keyFor(path)));
      img.setAttribute('data-mcdf-member', path);
      used.add(path);
      continue;
    }

    if (!missing.includes(path)) missing.push(path);
    replaceWithMarker(img, `Missing asset: ${path}`);
  }

  cache.prune(used);
  return { html: doc.body.innerHTML, missing, blocked };
}
