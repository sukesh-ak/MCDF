<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Diff: what changed since the document was last opened or saved.

  Two halves that answer different questions. The member table is the one that
  matters for trust — it is derived from digests, so it covers images and any
  other binary member a text diff cannot show. The merge view below it is the
  human-readable view of `content.md`, loaded on demand because @codemirror/merge
  is a large module nobody needs until they open this panel.
-->
<script lang="ts">
  import { untrack } from 'svelte';

  import Panel from '../components/Panel.svelte';
  import type { DocumentStore } from '../state/document.svelte.ts';

  interface Props {
    store: DocumentStore;
    onerror?: (message: string) => void;
  }

  let { store, onerror }: Props = $props();

  let host: HTMLDivElement | undefined = $state();
  let showText = $state(false);
  let loading = $state(false);

  let changes = $derived(store.changedMembers);
  let baseline = $derived(store.baselineContent);
  let current = $derived(store.workingContent);
  let textChanged = $derived(baseline !== current);

  /**
   * Builds the merge view when the panel asks for it.
   *
   * The two texts are read through `untrack`: a `$effect` that tracked them
   * would tear down and rebuild the whole widget on every keystroke, which is
   * the exact CodeMirror trap the editor component already documents.
   * Re-mounting on demand is honest here — this is a snapshot comparison, not a
   * live editor.
   */
  $effect(() => {
    const target = host;
    if (target === undefined || !showText) return;

    let view: { destroy: () => void } | undefined;
    let cancelled = false;
    const [a, b] = untrack(() => [baseline, current]);

    loading = true;
    void (async () => {
      try {
        const [{ MergeView }, { EditorState }, { EditorView, lineWrapping }] = await Promise.all([
          import('@codemirror/merge'),
          import('@codemirror/state'),
          import('@codemirror/view').then((m) => ({ ...m, lineWrapping: m.EditorView.lineWrapping })),
        ]);
        if (cancelled) return;
        // MergeView renders two `role="textbox"` surfaces. CodeMirror does not
        // name them, and an unnamed textbox is a WCAG 4.1.2 failure — axe
        // caught exactly this. Each side says which side it is.
        const side = (label: string) => [
          EditorState.readOnly.of(true),
          EditorView.editable.of(false),
          lineWrapping,
          EditorView.contentAttributes.of({ 'aria-label': label }),
        ];
        view = new MergeView({
          a: { doc: a, extensions: side('content.md as last saved') },
          b: { doc: b, extensions: side('content.md with the current edits') },
          parent: target,
          collapseUnchanged: { margin: 3, minSize: 6 },
        });
      } catch (e) {
        if (!cancelled) onerror?.(`Diff view: ${(e as Error).message}`);
      } finally {
        if (!cancelled) loading = false;
      }
    })();

    return () => {
      cancelled = true;
      view?.destroy();
    };
  });

  function refresh(): void {
    // Toggling off and on re-runs the effect against the current texts.
    showText = false;
    queueMicrotask(() => (showText = true));
  }
</script>

<Panel
  title="Diff"
  badge={changes.length === 0 ? 'unchanged' : `${changes.length} changed`}
  tone={changes.length === 0 ? 'neutral' : 'warn'}
  open={false}
>
  <p class="hint">
    Compared against the document as last opened or saved. Member changes come from digests, so
    images and other binary members are covered too.
  </p>

  {#if changes.length === 0}
    <p class="empty">Nothing has changed since the baseline.</p>
  {:else}
    <ul class="changes">
      {#each changes as change (change.path)}
        <li class={change.kind}>
          <span class="mark" aria-hidden="true">
            {change.kind === 'added' ? '+' : change.kind === 'removed' ? '−' : '~'}
          </span>
          <code>{change.path}</code>
          <span class="kind">{change.kind}</span>
        </li>
      {/each}
    </ul>
  {/if}

  {#if textChanged}
    <button type="button" onclick={() => (showText = !showText)} aria-expanded={showText}>
      {showText ? 'Hide' : 'Show'} content.md side by side
    </button>
    {#if showText}
      <button type="button" class="secondary" onclick={refresh}>Refresh</button>
      {#if loading}<p class="hint">Loading the comparison view…</p>{/if}
      <!--
        The merge view renders two labelled editors; the wrapper is a region so
        a screen-reader user can find and leave it as one unit. `aria-busy`
        rather than a second live region — see the editor pane in App.svelte.
      -->
      <div
        class="merge"
        bind:this={host}
        role="region"
        aria-busy={loading}
        aria-label="content.md, baseline on the left and current on the right"
      ></div>
      <p class="hint">
        Left: last saved. Right: now. Both are read-only — edit in the main editor.
      </p>
    {/if}
  {:else if changes.length > 0}
    <p class="hint">
      <code>content.md</code> itself is unchanged; the differences are in other members.
    </p>
  {/if}
</Panel>

<style>
  .hint,
  .empty {
    margin: 0.25rem 0 0.5rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }

  .changes {
    list-style: none;
    margin: 0.25rem 0 0.6rem;
    padding: 0;
    display: grid;
    gap: 0.2rem;
    font-size: 0.82rem;
  }

  .changes li {
    display: flex;
    align-items: baseline;
    gap: 0.4rem;
    min-width: 0;
    overflow-wrap: anywhere;
  }

  .changes li code {
    flex: 1;
  }

  .mark {
    font-family: var(--font-mono);
    font-weight: 700;
    width: 1ch;
  }

  li.added .mark {
    color: var(--ok);
  }

  li.removed .mark {
    color: var(--danger);
  }

  li.modified .mark {
    color: var(--warn);
  }

  .kind {
    font-size: 0.72rem;
    color: var(--fg-muted);
  }

  .merge {
    margin-top: 0.4rem;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: auto;
    max-height: 24rem;
    font-size: 0.8rem;
  }

  .merge :global(.cm-mergeView) {
    background: var(--bg);
  }

  .merge :global(.cm-editor) {
    font-family: var(--font-mono);
  }

  /* CM6's default diff tints are tuned for a light theme; route them through
     the app's tokens so dark mode stays readable and contrast stays above AA. */
  .merge :global(.cm-changedLine) {
    background: color-mix(in srgb, var(--warn) 14%, transparent);
  }

  .merge :global(.cm-deletedChunk) {
    background: color-mix(in srgb, var(--danger) 12%, transparent);
  }

  .merge :global(.cm-changedText) {
    background: color-mix(in srgb, var(--warn) 30%, transparent);
  }
</style>
