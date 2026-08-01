<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  The conversion report, shown before the imported document is adopted.

  This is a requirement of the import design rather than a nicety: an import is
  a best-effort conversion, and the user has to be told what survived and what
  did not *before* they start working — never silently. The same text is what
  the IMPORTED audit entry stands for, so this dialog and the document's own
  history tell the same story.

  A real modal dialog, hand-built rather than <dialog>: showModal() is well
  supported but its focus-trap and Escape behaviour still differ enough between
  engines that the accessibility bar here is easier to meet explicitly. Focus
  moves in on open, is trapped while open, and returns to the invoking control
  on close.
-->
<script lang="ts">
  import type { ImportReport } from 'mcdf-ts';

  interface Props {
    report: ImportReport;
    /** Name the document will take once adopted. */
    fileName: string;
    onaccept: () => void;
    oncancel: () => void;
  }

  let { report, fileName, onaccept, oncancel }: Props = $props();

  let dialog = $state<HTMLDivElement | undefined>();
  let accept = $state<HTMLButtonElement | undefined>();

  const FORMATS: Record<ImportReport['format'], string> = {
    markdown: 'Markdown',
    html: 'HTML',
    epub: 'EPUB',
  };

  // The element that had focus when the dialog opened, so it can be given back.
  // Losing focus to <body> on close strands a keyboard or screen-reader user at
  // the top of the page with no idea where they were.
  const opener = typeof document === 'undefined' ? null : document.activeElement;

  $effect(() => {
    accept?.focus();
    return () => {
      if (opener instanceof HTMLElement) opener.focus();
    };
  });

  /**
   * Keeps Tab inside the dialog.
   *
   * Without this the next Tab lands on the toolbar behind the overlay, which is
   * inert to the eye and fully reachable to the keyboard — the classic modal
   * accessibility failure.
   */
  function onKeyDown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      event.preventDefault();
      oncancel();
      return;
    }
    if (event.key !== 'Tab' || dialog === undefined) return;

    const focusable = [
      ...dialog.querySelectorAll<HTMLElement>(
        'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])',
      ),
    ].filter((el) => !el.hasAttribute('disabled'));
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (first === undefined || last === undefined) return;

    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  }

  let summary = $derived(
    [
      `${report.chapters} ${report.chapters === 1 ? 'section' : 'sections'}`,
      `${report.images} ${report.images === 1 ? 'image' : 'images'}`,
    ].join(' · '),
  );
</script>

<svelte:window on:keydown={onKeyDown} />

<!--
  The backdrop is decorative; it carries no click handler, because a
  click-to-dismiss surface that no keyboard user can reach is not a control.
  Escape and the two buttons are the ways out.
-->
<div class="backdrop"></div>

<div
  class="dialog"
  bind:this={dialog}
  role="dialog"
  aria-modal="true"
  aria-labelledby="conversion-heading"
  aria-describedby="conversion-intro"
>
  <h2 id="conversion-heading">Imported from {FORMATS[report.format]}</h2>

  <p id="conversion-intro">
    {#if report.source !== ''}<strong>{report.source}</strong> was converted{:else}The file was
      converted{/if}
    into <strong>{fileName}</strong>. Conversion is best-effort — check what came across before
    you continue.
  </p>

  <dl class="facts">
    <div>
      <dt>Title</dt>
      <dd>{report.title === '' ? '(none found)' : report.title}</dd>
    </div>
    {#if report.authors.length > 0}
      <div>
        <dt>{report.authors.length === 1 ? 'Author' : 'Authors'}</dt>
        <dd>{report.authors.join(', ')}</dd>
      </div>
    {/if}
    <div>
      <dt>Content</dt>
      <dd>{summary}</dd>
    </div>
  </dl>

  {#if report.notes.length > 0}
    <h3>What did not come across</h3>
    <ul class="notes">
      {#each report.notes as note, i (i)}
        <li>{note}</li>
      {/each}
    </ul>
  {/if}

  <p class="provenance">
    The new document records that it was imported: its metadata names the converter, and its
    audit log opens with an <code>IMPORTED</code> entry.
  </p>

  <div class="actions">
    <button type="button" bind:this={accept} class="primary" onclick={onaccept}>
      Open the document
    </button>
    <button type="button" onclick={oncancel}>Discard</button>
  </div>
</div>

<style>
  .backdrop {
    position: fixed;
    inset: 0;
    background: rgb(0 0 0 / 55%);
    z-index: 10;
  }

  .dialog {
    position: fixed;
    z-index: 11;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    width: min(34rem, calc(100vw - 2rem));
    max-height: calc(100vh - 2rem);
    overflow-y: auto;
    padding: 1rem 1.25rem 1.25rem;
    background: var(--bg-raised);
    border: 1px solid var(--border-strong);
    border-radius: var(--radius);
    box-shadow: 0 1rem 3rem rgb(0 0 0 / 35%);
  }

  h2 {
    margin: 0 0 0.5rem;
    font-size: 1.05rem;
  }

  h3 {
    margin: 1rem 0 0.35rem;
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    color: var(--fg-muted);
  }

  p {
    margin: 0 0 0.75rem;
    font-size: 0.9rem;
  }

  .facts {
    display: grid;
    gap: 0.3rem;
    margin: 0;
    padding: 0.6rem 0.75rem;
    background: var(--bg-sunken);
    border-radius: var(--radius);
    font-size: 0.88rem;
  }

  .facts div {
    display: flex;
    gap: 0.5rem;
  }

  .facts dt {
    flex: 0 0 5rem;
    color: var(--fg-muted);
  }

  .facts dd {
    margin: 0;
    min-width: 0;
    overflow-wrap: anywhere;
  }

  .notes {
    margin: 0;
    padding-left: 1.1rem;
    font-size: 0.88rem;
  }

  .notes li {
    margin-bottom: 0.25rem;
  }

  .provenance {
    margin-top: 0.9rem;
    font-size: 0.82rem;
    color: var(--fg-muted);
  }

  .actions {
    display: flex;
    gap: 0.5rem;
    margin-top: 1rem;
  }

  @media (prefers-reduced-motion: no-preference) {
    .dialog {
      animation: appear 120ms ease-out;
    }
  }

  @keyframes appear {
    from {
      opacity: 0;
      transform: translate(-50%, calc(-50% + 0.5rem));
    }
  }
</style>
