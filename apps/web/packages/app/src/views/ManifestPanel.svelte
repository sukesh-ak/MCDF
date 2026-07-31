<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Manifest: every member with a live digest, and a dot showing whether it still
  matches what manifest.json recorded. Hashes are recomputed from the working
  bytes on every edit, so the panel goes stale the instant the document does.

  Colour is never the only channel: each state also has a shape, a text label in
  the row, and a column header a screen reader can read out.
-->
<script lang="ts">
  import Panel from '../components/Panel.svelte';
  import type { DocumentStore, MemberState } from '../state/document.svelte.ts';

  interface Props {
    store: DocumentStore;
    onmanifestrebuilt?: () => void;
  }

  let { store, onmanifestrebuilt }: Props = $props();

  /** Full wording, for assistive technology and the legend. */
  const LABELS: Record<MemberState, string> = {
    unchanged: 'Matches manifest',
    modified: 'Changed since manifest',
    unlisted: 'Not listed in manifest',
    excluded: 'Excluded from manifest',
  };

  /** Short wording, so the status column fits a sidebar. */
  const SHORT: Record<MemberState, string> = {
    unchanged: 'OK',
    modified: 'Changed',
    unlisted: 'Unlisted',
    excluded: 'Excluded',
  };

  const MARKS: Record<MemberState, string> = {
    unchanged: '●',
    modified: '▲',
    unlisted: '■',
    excluded: '–',
  };

  let changed = $derived(store.members.filter((m) => m.state === 'modified' || m.state === 'unlisted'));

  function shortHash(hex: string): string {
    return hex === '' ? '—' : `${hex.slice(0, 8)}…`;
  }

  function formatSize(bytes: number): string {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
    return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
  }

  function rebuild(): void {
    store.addManifest();
    onmanifestrebuilt?.();
  }
</script>

<Panel
  title="Manifest"
  badge={store.hasManifest
    ? store.manifestUpToDate
      ? 'up to date'
      : `${changed.length + store.missingMembers.length} stale`
    : 'absent'}
  tone={store.hasManifest ? (store.manifestUpToDate ? 'ok' : 'warn') : 'warn'}
>
  {#if !store.hasManifest}
    <p class="empty">
      No <code>manifest.json</code>. The document is valid at the Core profile but cannot be
      integrity-checked or signed. Adding one moves it to the Integrity profile.
    </p>
    <button type="button" onclick={rebuild}>Add manifest (Core → Integrity)</button>
  {:else}
    <!--
      Three columns, not five: this panel lives in a sidebar, and a table wider
      than its container pushes a horizontal scrollbar onto the whole page.
      Size and digest share a "Details" cell, and long member paths wrap rather
      than forcing the table wide.
    -->
    <div class="table-scroll">
      <table>
        <caption class="visually-hidden">
          Container members with their digests and manifest status
        </caption>
        <colgroup>
          <col class="col-status" />
          <col class="col-member" />
          <col class="col-details" />
        </colgroup>
        <thead>
          <tr>
            <th scope="col">Status</th>
            <th scope="col">Member</th>
            <th scope="col">Size &amp; SHA-256</th>
          </tr>
        </thead>
        <tbody>
          {#each store.members as member (member.path)}
            <tr class={member.state}>
              <td>
                <span class="mark" aria-hidden="true">{MARKS[member.state]}</span>
                <span class="state-label">{SHORT[member.state]}</span>
                <span class="visually-hidden">{LABELS[member.state]}</span>
              </td>
              <th scope="row"><code>{member.path}</code></th>
              <td class="details">
                <span class="size">{formatSize(member.size)}</span>
                <code class="hash" title={member.digest}>{shortHash(member.digest)}</code>
              </td>
            </tr>
          {/each}
          {#each store.missingMembers as path (path)}
            <tr class="missing">
              <td>
                <span class="mark" aria-hidden="true">✕</span>
                <span class="state-label">Missing</span>
                <span class="visually-hidden">Listed in the manifest but missing</span>
              </td>
              <th scope="row"><code>{path}</code></th>
              <td class="details"><span class="size">—</span></td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>

    <dl class="summary">
      <dt>Container hash</dt>
      <dd><code class="hash" title={store.containerHash}>{shortHash(store.containerHash)}</code></dd>
      <dt>Packed size</dt>
      <dd>{formatSize(store.packedSize)}</dd>
    </dl>

    {#if !store.manifestUpToDate}
      <p class="stale">
        <strong>Saving rebuilds the manifest automatically</strong> — you do not need to do
        anything. Until then the rows above show what changed, because a stale manifest is the
        signal that content moved: rebuilding it on every keystroke would hide that, and would
        silently invalidate a signature that still covers the old bytes.
      </p>
      <button type="button" onclick={rebuild}>Recompute now, without saving</button>
    {/if}
  {/if}
</Panel>

<style>
  .empty {
    color: var(--fg-muted);
    margin: 0.25rem 0 0.5rem;
  }

  /* Last-resort guard: if the sidebar is ever narrower than the minimum the
     table can shrink to, it scrolls inside its own box rather than widening the
     page. */
  .table-scroll {
    overflow-x: auto;
    max-width: 100%;
  }

  table {
    width: 100%;
    table-layout: fixed;
    border-collapse: collapse;
    font-size: 0.82rem;
  }

  .col-status {
    width: 4.6rem;
  }

  .col-details {
    width: 7.5rem;
  }

  th,
  td {
    text-align: left;
    padding: 0.25rem 0.35rem;
    border-bottom: 1px solid var(--border);
    vertical-align: top;
  }

  thead th {
    font-size: 0.72rem;
    color: var(--fg-muted);
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  /* Member paths have no spaces to break at, so they must be allowed to break
     mid-token or they set the table's minimum width. */
  tbody th {
    font-weight: 400;
    overflow-wrap: anywhere;
    word-break: break-word;
  }

  .details {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
  }

  .size {
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
    color: var(--fg-muted);
    font-size: 0.75rem;
  }

  .hash {
    font-family: var(--font-mono);
    font-size: 0.75rem;
    color: var(--fg-muted);
    overflow-wrap: anywhere;
  }

  .mark {
    font-size: 0.7rem;
    margin-right: 0.3rem;
  }

  /* The label is short-form on wide screens and read in full by assistive tech. */
  .state-label {
    font-size: 0.72rem;
    color: var(--fg-muted);
  }

  tr.unchanged .mark {
    color: var(--ok);
  }

  tr.modified .mark,
  tr.unlisted .mark {
    color: var(--warn);
  }

  tr.missing .mark {
    color: var(--danger);
  }

  tr.modified,
  tr.unlisted {
    background: var(--warn-bg);
  }

  tr.missing {
    background: var(--danger-bg);
  }

  .summary {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr);
    gap: 0.15rem 0.6rem;
    margin: 0.6rem 0 0;
    font-size: 0.8rem;
  }

  .summary dt {
    color: var(--fg-muted);
  }

  .summary dd {
    margin: 0;
    overflow-wrap: anywhere;
  }

  .stale {
    margin: 0.6rem 0 0.4rem;
    font-size: 0.85rem;
    color: var(--warn);
  }
</style>
