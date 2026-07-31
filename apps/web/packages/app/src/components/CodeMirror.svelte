<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  CodeMirror 6 wrapper. CM6 owns its DOM imperatively, which is exactly why this
  client is built on a compile-away framework — there is no virtual DOM to hold
  off with refs. CM6 also carries the accessibility work a
  <textarea> lacks: it exposes a real textbox role, keeps arrow-key and
  screen-reader navigation working, and honours the user's caret settings.
-->
<script lang="ts">
  import { defaultKeymap, history, historyKeymap } from '@codemirror/commands';
  import { markdown } from '@codemirror/lang-markdown';
  import { EditorState, type Extension } from '@codemirror/state';
  import { EditorView, keymap, lineNumbers, highlightActiveLine } from '@codemirror/view';
  import { untrack } from 'svelte';

  interface Props {
    value: string;
    label: string;
    /** Element id of a description associated with the editor. */
    describedBy?: string;
    readOnly?: boolean;
    onchange?: (value: string) => void;
  }

  let { value = $bindable(), label, describedBy, readOnly = false, onchange }: Props = $props();

  let host: HTMLDivElement | undefined = $state();
  let view: EditorView | undefined = $state();
  /**
   * The last text this component put into `value` itself. The sync effect uses
   * it to tell an external change (a document was opened) from the echo of the
   * user's own keystroke.
   */
  let emitted = '';

  function extensions(): Extension[] {
    return [
      lineNumbers(),
      highlightActiveLine(),
      history(),
      keymap.of([...defaultKeymap, ...historyKeymap]),
      markdown(),
      EditorView.lineWrapping,
      EditorState.readOnly.of(readOnly),
      EditorView.contentAttributes.of({
        'aria-label': label,
        ...(describedBy === undefined ? {} : { 'aria-describedby': describedBy }),
      }),
      EditorView.updateListener.of((update) => {
        if (!update.docChanged) return;
        const text = update.state.doc.toString();
        emitted = text;
        value = text;
        onchange?.(text);
      }),
    ];
  }

  // Create the editor once, for as long as the host element lives.
  //
  // `host` is the ONLY thing this effect may track. Reading `value` (or any
  // other prop) here makes every keystroke re-run the effect, which tears the
  // editor down and builds a new one — the DOM node being typed into disappears
  // mid-input, so the caret is lost and everything after the first character is
  // dropped. `untrack` states that intent rather than relying on nobody
  // reintroducing a read later.
  $effect(() => {
    const target = host;
    if (target === undefined) return;

    const instance = untrack(
      () =>
        new EditorView({
          state: EditorState.create({ doc: value, extensions: extensions() }),
          parent: target,
        }),
    );
    emitted = instance.state.doc.toString();
    view = instance;
    return () => {
      instance.destroy();
      view = undefined;
    };
  });

  // Push external changes (opening a different document) into the editor. The
  // echo of the user's own typing is skipped, so this never dispatches over a
  // caret that is mid-edit.
  $effect(() => {
    const next = value;
    const instance = view;
    if (instance === undefined || next === emitted) return;
    const current = instance.state.doc.toString();
    if (current === next) return;
    instance.dispatch({ changes: { from: 0, to: current.length, insert: next } });
    emitted = next;
  });

  export function focus(): void {
    view?.focus();
  }

  /** Moves the caret to the start of a 1-based line and scrolls it into view. */
  export function goToLine(line: number): void {
    const instance = view;
    if (instance === undefined) return;
    const clamped = Math.min(Math.max(1, line), instance.state.doc.lines);
    const pos = instance.state.doc.line(clamped).from;
    instance.dispatch({
      selection: { anchor: pos },
      effects: EditorView.scrollIntoView(pos, { y: 'center' }),
    });
    instance.focus();
  }
</script>

<div class="editor" bind:this={host}></div>

<style>
  .editor {
    height: 100%;
    overflow: auto;
    background: var(--bg);
  }

  .editor :global(.cm-editor) {
    height: 100%;
    font-family: var(--font-mono);
    font-size: 0.95rem;
  }

  /* CM6 draws its own focus outline on the scroller; route it to the shared
     token so the indicator matches every other control. */
  .editor :global(.cm-editor.cm-focused) {
    outline: 2px solid var(--focus);
    outline-offset: -2px;
  }

  .editor :global(.cm-gutters) {
    background: var(--bg-sunken);
    color: var(--fg-muted);
    border-right: 1px solid var(--border);
  }

  .editor :global(.cm-activeLine) {
    background: color-mix(in srgb, var(--accent) 8%, transparent);
  }

  .editor :global(.cm-cursor) {
    border-left-color: var(--fg);
  }

  .editor :global(.cm-selectionBackground),
  .editor :global(.cm-content ::selection) {
    background: color-mix(in srgb, var(--accent) 28%, transparent) !important;
  }
</style>
