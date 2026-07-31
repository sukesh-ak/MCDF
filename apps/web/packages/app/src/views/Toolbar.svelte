<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
-->
<script lang="ts">
  type View = 'editor' | 'split' | 'preview';
  type Theme = 'system' | 'light' | 'dark';

  interface Props {
    fileName: string;
    dirty: boolean;
    canSaveInPlace: boolean;
    view: View;
    theme: Theme;
    onnew: () => void;
    onopen: () => void;
    onsave: () => void;
    onsaveas: () => void;
    onviewchange: (view: View) => void;
    onthemechange: (theme: Theme) => void;
  }

  const VIEWS: { value: View; label: string }[] = [
    { value: 'editor', label: 'Editor' },
    { value: 'split', label: 'Split' },
    { value: 'preview', label: 'Preview' },
  ];

  function pickTheme(event: Event): void {
    const target = event.currentTarget as HTMLSelectElement;
    onthemechange(target.value as Theme);
  }

  let {
    fileName,
    dirty,
    canSaveInPlace,
    view,
    theme,
    onnew,
    onopen,
    onsave,
    onsaveas,
    onviewchange,
    onthemechange,
  }: Props = $props();
</script>

<div class="toolbar">
  <div class="group">
    <button type="button" onclick={onnew}>New</button>
    <button type="button" onclick={onopen}>Open…</button>
    <button type="button" class="primary" onclick={onsave} disabled={!dirty && canSaveInPlace}>
      Save
    </button>
    <button type="button" onclick={onsaveas}>Save as…</button>
  </div>

  <p class="filename">
    <span class="name">{fileName}</span>
    {#if dirty}
      <span class="dirty">unsaved changes</span>
    {/if}
  </p>

  <fieldset class="group segmented">
    <legend class="visually-hidden">Layout</legend>
    {#each VIEWS as option (option.value)}
      <label class="segment" class:selected={view === option.value}>
        <input
          type="radio"
          name="view"
          value={option.value}
          checked={view === option.value}
          onchange={() => onviewchange(option.value)}
        />
        {option.label}
      </label>
    {/each}
  </fieldset>

  <p class="field">
    <label for="theme">Theme</label>
    <select id="theme" value={theme} onchange={pickTheme}>
      <option value="system">Match system</option>
      <option value="light">Light</option>
      <option value="dark">Dark</option>
    </select>
  </p>
</div>

<style>
  .toolbar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--gap);
    padding: 0.5rem 0.75rem;
    background: var(--bg-raised);
    border-bottom: 1px solid var(--border);
  }

  .group {
    display: flex;
    gap: 0.35rem;
    margin: 0;
    padding: 0;
    border: 0;
  }

  .filename {
    flex: 1;
    min-width: 8rem;
    margin: 0;
    font-size: 0.85rem;
    color: var(--fg-muted);
    display: flex;
    gap: 0.5rem;
    align-items: baseline;
  }

  .name {
    font-family: var(--font-mono);
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .dirty {
    color: var(--warn);
    font-weight: 600;
    white-space: nowrap;
  }

  .segmented {
    border: 1px solid var(--border-strong);
    border-radius: var(--radius);
    overflow: hidden;
    gap: 0;
  }

  .segment {
    display: inline-flex;
    align-items: center;
    min-height: var(--target);
    padding: 0.3rem 0.6rem;
    font-size: 0.85rem;
    cursor: pointer;
    border-right: 1px solid var(--border-strong);
  }

  .segment:last-child {
    border-right: 0;
  }

  .segment.selected {
    background: var(--accent);
    color: var(--accent-fg);
    font-weight: 600;
  }

  /* The radio stays in the accessibility tree and keeps arrow-key group
     navigation; only its default painting is replaced. */
  .segment input {
    position: absolute;
    opacity: 0;
    width: 1px;
    height: 1px;
  }

  .segment:has(input:focus-visible) {
    outline: 2px solid var(--focus);
    outline-offset: -2px;
  }

  .field {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin: 0;
  }

  .field label {
    font-size: 0.8rem;
    color: var(--fg-muted);
  }

  .field select {
    width: auto;
  }
</style>
