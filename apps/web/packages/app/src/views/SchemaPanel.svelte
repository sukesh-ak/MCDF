<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Schema: the logical structure a document claims to have. Each section `id`
  binds to a heading anchor in content.md, and a section that binds to nothing is
  what raises E_SCHEMA_UNBOUND / E_REQUIRED_SECTION_MISSING.

  This panel is the other half of that repair. The Structure panel fixes a
  mismatch by anchoring a heading; here you fix it by changing what the document
  claims — which is the right move when the content is correct and the schema is
  the stale part (a schema copied from a different document, say).
-->
<script lang="ts">
  import Panel from '../components/Panel.svelte';
  import type { DocumentStore } from '../state/document.svelte.ts';

  interface Props {
    store: DocumentStore;
    onadopted?: (count: number) => void;
  }

  let { store, onadopted }: Props = $props();

  let boundIds = $derived(new Set(store.headings.map((h) => h.id).filter((id) => id !== '')));
  let anchoredHeadings = $derived(store.headings.filter((h) => h.id !== '').length);

  function adopt(): void {
    store.adoptSchemaFromContent();
    onadopted?.(anchoredHeadings);
  }
</script>

<Panel
  title="Schema"
  badge={store.hasSchema ? `${store.schema.sections.length} sections` : 'absent'}
  tone={store.hasSchema ? 'neutral' : 'warn'}
>
  {#if !store.hasSchema}
    <p class="empty">
      This document has no <code>schema.yaml</code>, so nothing constrains its structure.
    </p>
    <button type="button" onclick={() => store.addSchema()}>Add schema</button>
  {:else}
    <p class="field">
      <label for="doc-type">Document type</label>
      <input
        id="doc-type"
        type="text"
        placeholder="contract, report, policy…"
        bind:value={store.schema.document_type}
      />
    </p>

    <h3 class="sub">Sections</h3>
    {#if store.schema.sections.length === 0}
      <p class="empty">No sections declared.</p>
    {:else}
      <ol class="sections">
        {#each store.schema.sections as section, i (i)}
          <li class:bound={boundIds.has(section.id)} class:unbound={!boundIds.has(section.id)}>
            <p class="field">
              <label for="section-id-{i}">Anchor id</label>
              <input id="section-id-{i}" type="text" bind:value={section.id} />
            </p>
            <p class="field">
              <label for="section-title-{i}">Title</label>
              <input id="section-title-{i}" type="text" bind:value={section.title} />
            </p>
            <p class="checkbox">
              <input id="section-required-{i}" type="checkbox" bind:checked={section.required} />
              <label for="section-required-{i}">Required</label>
            </p>
            <p class="status">
              {#if section.id === ''}
                <span class="warn-text">No id — this section can never bind.</span>
              {:else if boundIds.has(section.id)}
                <span class="ok-text">Bound to a heading.</span>
              {:else}
                <span class="warn-text">
                  No heading carries <code>{'{'}#{section.id}{'}'}</code>.
                </span>
              {/if}
            </p>
            <p class="actions">
              <button type="button" onclick={() => store.moveSection(i, -1)} disabled={i === 0}>
                Move up<span class="visually-hidden">: section {i + 1}</span>
              </button>
              <button
                type="button"
                onclick={() => store.moveSection(i, 1)}
                disabled={i === store.schema.sections.length - 1}
              >
                Move down<span class="visually-hidden">: section {i + 1}</span>
              </button>
              <button type="button" onclick={() => store.removeSection(i)}>
                Remove<span class="visually-hidden">
                  section {i + 1}{section.title ? `, ${section.title}` : ''}</span
                >
              </button>
            </p>
          </li>
        {/each}
      </ol>
    {/if}

    <p class="actions">
      <button type="button" onclick={() => store.addSection()}>Add section</button>
      <button type="button" onclick={adopt} disabled={anchoredHeadings === 0}>
        Rebuild from content
      </button>
    </p>
    <p class="hint">
      <strong>Rebuild from content</strong> replaces every section with one per anchored heading
      ({anchoredHeadings} found). Use it when the schema belongs to a different document than the
      content does.
    </p>
  {/if}
</Panel>

<style>
  .empty {
    color: var(--fg-muted);
    margin: 0.25rem 0 0.5rem;
  }

  .field {
    display: grid;
    gap: 0.15rem;
    margin: 0 0 0.4rem;
  }

  .field label {
    font-size: 0.8rem;
    color: var(--fg-muted);
  }

  .sub {
    font-size: 0.85rem;
    margin: 0.75rem 0 0.35rem;
  }

  .sections {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    gap: 0.5rem;
  }

  .sections li {
    padding: 0.5rem 0.6rem;
    border: 1px solid var(--border);
    border-left-width: 3px;
    border-radius: var(--radius);
    background: var(--bg-sunken);
  }

  .sections li.bound {
    border-left-color: var(--ok);
  }

  .sections li.unbound {
    border-left-color: var(--warn);
  }

  .checkbox {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin: 0 0 0.35rem;
  }

  .checkbox input {
    width: auto;
    min-height: auto;
  }

  .checkbox label {
    font-size: 0.8rem;
  }

  .status {
    margin: 0 0 0.4rem;
    font-size: 0.78rem;
  }

  .ok-text {
    color: var(--ok);
  }

  .warn-text {
    color: var(--warn);
  }

  .actions {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35rem;
    margin: 0;
  }

  .actions button {
    font-size: 0.8rem;
  }

  .hint {
    margin: 0.5rem 0 0;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }
</style>
