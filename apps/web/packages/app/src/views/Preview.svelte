<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Live CommonMark preview.

  Injecting HTML is safe here because mcdf-ts renders with micromark's defaults:
  raw HTML is escaped rather than passed through, and dangerous URL protocols are
  dropped, so document content cannot smuggle in script. See the tests in
  mcdf-ts/src/serialize/markdown.test.ts, which pin that behaviour.

  This is the *preview*. The canonical render — the bytes a Render-profile
  validator checks — is a separate concern, still open.
-->
<script lang="ts">
  import { AssetUrlCache, resolveAssetUrls } from '../platform/assets.ts';
  import type { DocumentStore } from '../state/document.svelte.ts';

  interface Props {
    store: DocumentStore;
  }

  let { store }: Props = $props();

  const cache = new AssetUrlCache();
  $effect(() => () => cache.revokeAll());

  // Container-relative image paths are resolved to blob URLs over the member
  // bytes; without this the browser would resolve them against the page origin
  // and every embedded image would 404.
  let resolved = $derived(
    resolveAssetUrls(
      store.previewHtml,
      {
        read: (path) => store.tryReadMember(path),
        keyFor: (path) => store.memberDigests.get(path) ?? '',
      },
      cache,
    ),
  );
</script>

<div class="preview">
  {#if resolved.missing.length > 0}
    <p class="missing-summary" role="status">
      {resolved.missing.length}
      {resolved.missing.length === 1 ? 'image is' : 'images are'} referenced but not in this
      container:
      {resolved.missing.join(', ')}. Use <strong>Attach asset…</strong> to add
      {resolved.missing.length === 1 ? 'it' : 'them'}.
    </p>
  {/if}
  {#if resolved.blocked.length > 0}
    <p class="missing-summary" role="status">
      {resolved.blocked.length}
      external {resolved.blocked.length === 1 ? 'image was' : 'images were'} not loaded. A document
      must carry its own assets, so nothing is fetched from another host — attach the
      {resolved.blocked.length === 1 ? 'file' : 'files'} to the container instead.
    </p>
  {/if}
  <article class="rendered">
    {@html resolved.html}
  </article>
</div>

<style>
  .preview {
    height: 100%;
    overflow: auto;
    background: var(--bg);
    padding: 1rem 1.25rem 3rem;
  }

  /* Fills the pane. A centred measure reads better for long prose, but this pane
     is already narrowed by the editor and the sidebar, so capping it again just
     wasted the space the user gave the preview. */
  .rendered {
    max-width: none;
    margin: 0;
  }

  .rendered :global(h1),
  .rendered :global(h2),
  .rendered :global(h3),
  .rendered :global(h4),
  .rendered :global(h5),
  .rendered :global(h6) {
    line-height: 1.25;
    margin: 1.4em 0 0.5em;
  }

  .rendered :global(h1:first-child) {
    margin-top: 0;
  }

  .rendered :global(pre) {
    background: var(--bg-sunken);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.6rem 0.75rem;
    overflow-x: auto;
  }

  .rendered :global(code) {
    font-family: var(--font-mono);
    font-size: 0.9em;
  }

  .rendered :global(:not(pre) > code) {
    background: var(--bg-sunken);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.05em 0.3em;
  }

  .rendered :global(blockquote) {
    margin: 1em 0;
    padding-left: 1rem;
    border-left: 4px solid var(--border-strong);
    color: var(--fg-muted);
  }

  .rendered :global(table) {
    border-collapse: collapse;
    display: block;
    overflow-x: auto;
    max-width: 100%;
  }

  .rendered :global(th),
  .rendered :global(td) {
    border: 1px solid var(--border);
    padding: 0.3rem 0.5rem;
  }

  .rendered :global(img) {
    max-width: 100%;
    height: auto;
  }

  .rendered :global(hr) {
    border: 0;
    border-top: 1px solid var(--border);
  }

  /* A referenced-but-absent asset says so, rather than leaving a blank box. */
  .rendered :global(.mcdf-missing-asset) {
    display: inline-block;
    padding: 0.2rem 0.5rem;
    background: var(--warn-bg);
    color: var(--warn);
    border: 1px dashed currentColor;
    border-radius: var(--radius);
    font-size: 0.85rem;
  }

  .missing-summary {
    margin: 0 0 1rem;
    padding: 0.45rem 0.6rem;
    background: var(--warn-bg);
    color: var(--warn);
    border: 1px solid var(--warn);
    border-radius: var(--radius);
    font-size: 0.85rem;
  }
</style>
