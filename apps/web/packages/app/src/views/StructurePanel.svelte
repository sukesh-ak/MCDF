<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Structure: the document outline with schema binding state. A schema section
  whose id has no matching heading anchor is exactly what raises
  E_SCHEMA_UNBOUND / E_REQUIRED_SECTION_MISSING, so the panel shows the failure
  before validation does.
-->
<script lang="ts">
  import Panel from '../components/Panel.svelte';
  import type { DocumentStore } from '../state/document.svelte.ts';

  interface Props {
    store: DocumentStore;
    onjump?: (heading: string, line: number) => void;
    onbound?: (sectionId: string, headingText: string) => void;
  }

  let { store, onjump, onbound }: Props = $props();

  let unbound = $derived(store.unboundSections);
  let missingRequired = $derived(unbound.filter((s) => s.required).length);

  /** Chosen heading line per unbound section id, for the bind control. */
  let target = $state<Record<string, string>>({});

  function bind(sectionId: string): void {
    const line = Number.parseInt(target[sectionId] ?? '', 10);
    if (!Number.isFinite(line)) return;
    const heading = store.headings.find((h) => h.line === line);
    store.bindSection(sectionId, line);
    onbound?.(sectionId, heading?.text ?? `line ${line}`);
  }
</script>

<Panel
  title="Structure"
  badge={unbound.length === 0 ? `${store.headings.length} headings` : `${unbound.length} unbound`}
  tone={missingRequired > 0 ? 'danger' : unbound.length > 0 ? 'warn' : 'neutral'}
>
  {#if store.headings.length === 0}
    <p class="empty">No headings yet. Start a line with <code>#</code> to add one.</p>
  {:else}
    <nav aria-label="Document outline">
      <ol class="outline">
        {#each store.outline as row (row.heading.text + row.heading.id)}
          <li style="--depth: {row.heading.level - 1}">
            <button
              type="button"
              class="jump"
              onclick={() => onjump?.(row.heading.text, row.heading.line)}
            >
              <span class="level" aria-hidden="true">H{row.heading.level}</span>
              <span class="text">{row.heading.text || '(untitled)'}</span>
              <span class="visually-hidden">, heading level {row.heading.level}</span>
            </button>
            {#if row.heading.id !== ''}
              <span class="anchor" title="Heading anchor">#{row.heading.id}</span>
            {/if}
            {#if row.sectionTitle !== null}
              <span class="bound">
                bound{row.required ? ' · required' : ''}
              </span>
            {/if}
          </li>
        {/each}
      </ol>
    </nav>
  {/if}

  {#if unbound.length > 0}
    <h3 class="sub">Schema sections with no matching heading</h3>
    <p class="sub-note">
      The schema declares these sections but no heading carries the matching anchor. Bind one
      below, or edit the schema itself in the Schema panel.
    </p>
    <ul class="unbound">
      {#each unbound as section (section.id)}
        <li>
          <p class="row">
            <code>#{section.id}</code>
            <span class="code {section.required ? 'danger' : 'warn'}">
              {section.required ? 'E_REQUIRED_SECTION_MISSING' : 'E_SCHEMA_UNBOUND'}
            </span>
          </p>
          {#if store.headings.length === 0}
            <p class="hint">Add a heading to <code>content.md</code> first.</p>
          {:else}
            <p class="bind">
              <label for="bind-{section.id}">
                Bind <code>#{section.id}</code> to
              </label>
              <span class="bind-controls">
                <select id="bind-{section.id}" bind:value={target[section.id]}>
                  <option value="">Choose a heading…</option>
                  {#each store.headings as heading (heading.line)}
                    <option value={String(heading.line)}>
                      {'—'.repeat(Math.max(0, heading.level - 1))}
                      {heading.text || '(untitled)'}
                    </option>
                  {/each}
                </select>
                <button
                  type="button"
                  onclick={() => bind(section.id)}
                  disabled={!target[section.id]}
                >
                  Bind
                </button>
              </span>
            </p>
          {/if}
        </li>
      {/each}
    </ul>
  {/if}
</Panel>

<style>
  .empty {
    color: var(--fg-muted);
    margin: 0.25rem 0;
  }

  .outline {
    list-style: none;
    margin: 0;
    padding: 0;
  }

  .outline li {
    display: flex;
    align-items: baseline;
    gap: 0.4rem;
    padding-left: calc(var(--depth) * 0.85rem);
    margin: 0.1rem 0;
  }

  .jump {
    display: flex;
    align-items: baseline;
    gap: 0.4rem;
    flex: 1;
    min-width: 0;
    padding: 0.15rem 0.3rem;
    background: none;
    border: 1px solid transparent;
    text-align: left;
  }

  .jump:hover {
    background: var(--bg-sunken);
    border-color: var(--border);
  }

  .level {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    color: var(--fg-muted);
  }

  .text {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .anchor,
  .bound {
    font-size: 0.75rem;
    color: var(--fg-muted);
    font-family: var(--font-mono);
    white-space: nowrap;
  }

  .bound {
    color: var(--ok);
    font-family: var(--font-ui);
  }

  .sub {
    font-size: 0.85rem;
    margin: 0.75rem 0 0.25rem;
  }

  .sub-note {
    margin: 0 0 0.5rem;
    font-size: 0.8rem;
    color: var(--fg-muted);
  }

  .unbound {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    gap: 0.5rem;
  }

  .unbound li {
    display: grid;
    gap: 0.3rem;
    padding: 0.4rem 0.5rem;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-sunken);
    font-size: 0.85rem;
  }

  .row {
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 0.4rem;
    margin: 0;
  }

  .bind {
    display: grid;
    gap: 0.25rem;
    margin: 0;
  }

  .bind label {
    font-size: 0.78rem;
    color: var(--fg-muted);
  }

  .bind-controls {
    display: flex;
    gap: 0.35rem;
    min-width: 0;
  }

  .bind-controls select {
    min-width: 0;
    flex: 1;
  }

  .code {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    padding: 0.05rem 0.35rem;
    border-radius: 999px;
    border: 1px solid currentColor;
  }

  .code.warn {
    color: var(--warn);
    background: var(--warn-bg);
  }

  .code.danger {
    color: var(--danger);
    background: var(--danger-bg);
  }

  .hint {
    color: var(--fg-muted);
  }
</style>
