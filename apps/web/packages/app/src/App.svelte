<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  The application shell. Landmarks are real (banner / main / complementary /
  contentinfo), every panel is a titled section, and status changes reach a live
  region so integrity breaking is audible as well as visible.
-->
<script lang="ts">
  import type CodeMirror from './components/CodeMirror.svelte';
  import { applyServiceWorkerUpdate, registerServiceWorker } from './platform/pwa.ts';
  import {
    download,
    hasFileSystemAccess,
    isSecureContextAvailable,
    openDocument,
    openViaInput,
    readBlob,
    saveDocumentAs,
    writeHandle,
  } from './platform/files.ts';
  import {
    isImportable,
    pickForImport,
    runImport,
    type ImportOutcome,
    type PickedFile,
  } from './platform/import.ts';
  import { announcer } from './state/announcer.svelte.ts';
  import { DocumentStore } from './state/document.svelte.ts';
  import AuditPanel from './views/AuditPanel.svelte';
  import ConversionReport from './views/ConversionReport.svelte';
  import DiffPanel from './views/DiffPanel.svelte';
  import EncryptionPanel from './views/EncryptionPanel.svelte';
  import ManifestPanel from './views/ManifestPanel.svelte';
  import MetadataPanel from './views/MetadataPanel.svelte';
  import Preview from './views/Preview.svelte';
  import SchemaPanel from './views/SchemaPanel.svelte';
  import StructurePanel from './views/StructurePanel.svelte';
  import Toolbar from './views/Toolbar.svelte';
  import TrustPanel from './views/TrustPanel.svelte';
  import ValidationPanel from './views/ValidationPanel.svelte';

  type View = 'editor' | 'split' | 'preview';
  type Theme = 'system' | 'light' | 'dark';

  const store = new DocumentStore();

  let view = $state<View>('split');
  let theme = $state<Theme>('system');
  let editor = $state<CodeMirror | undefined>();
  /**
   * CodeMirror is the largest thing the app loads. Importing it on mount
   * instead of at module scope lets the shell, the panels and the preview paint
   * first; the editor slots in a moment later, which on a slow connection is
   * the difference between a blank page and a usable one.
   */
  let Editor = $state<typeof CodeMirror | undefined>();
  let updateReady = $state(false);
  let handle = $state<FileSystemFileHandle | null>(null);
  let error = $state('');
  /** Non-fatal: the file worked, just not through the preferred API. */
  let notice = $state('');
  let dragging = $state(false);
  let importing = $state(false);
  /**
   * A finished conversion, held until the user has seen the report.
   *
   * Nothing is adopted before they accept it: an import is a lossy conversion,
   * and replacing the open document with one silently would be the exact
   * "never silently" the import rules forbid.
   */
  let pendingImport = $state<ImportOutcome | null>(null);

  const supportsPicker = hasFileSystemAccess();
  const secure = isSecureContextAvailable();

  $effect(() => {
    void import('./components/CodeMirror.svelte').then((m) => (Editor = m.default));
  });

  $effect(() => {
    registerServiceWorker((status) => (updateReady = status.updateReady));
  });

  // Persist the theme choice; the system preference is the default.
  $effect(() => {
    const stored = localStorage.getItem('mcdf.theme');
    if (stored === 'light' || stored === 'dark' || stored === 'system') theme = stored;
  });

  $effect(() => {
    if (theme === 'system') {
      document.documentElement.removeAttribute('data-theme');
    } else {
      document.documentElement.setAttribute('data-theme', theme);
    }
    localStorage.setItem('mcdf.theme', theme);
  });

  // Warn before losing unsaved work.
  $effect(() => {
    const dirty = store.dirty;
    const onBeforeUnload = (e: BeforeUnloadEvent): void => {
      if (!dirty) return;
      e.preventDefault();
    };
    window.addEventListener('beforeunload', onBeforeUnload);
    return () => window.removeEventListener('beforeunload', onBeforeUnload);
  });

  // Ctrl/Cmd+S saves, matching every other editor.
  $effect(() => {
    const onKeyDown = (e: KeyboardEvent): void => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        void save();
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  });

  // Announce integrity going stale — the tamper demo has to be audible.
  let lastIntegrity = $state<boolean | null>(null);
  $effect(() => {
    if (!store.hasManifest) return;
    const ok = store.manifestUpToDate;
    if (lastIntegrity === null) {
      lastIntegrity = ok;
      return;
    }
    if (ok === lastIntegrity) return;
    lastIntegrity = ok;
    announcer.say(
      ok
        ? 'Manifest matches the document again.'
        : 'Manifest no longer matches the document. Any signature over it is now invalid.',
      'assertive',
    );
  });

  // The same, one level up: a signed document losing its signature is the most
  // consequential thing this app can report, so it is said out loud too.
  let lastTrust = $state<boolean | null>(null);
  $effect(() => {
    const signed = store.signatures.length > 0;
    const ok = signed && store.signedAndValid;
    if (!signed) {
      lastTrust = null;
      return;
    }
    if (lastTrust === null) {
      lastTrust = ok;
      return;
    }
    if (ok === lastTrust) return;
    lastTrust = ok;
    announcer.say(
      ok
        ? 'Signatures verify again.'
        : 'Signature no longer verifies: this document has changed since it was signed.',
      'assertive',
    );
  });

  function fail(e: unknown): void {
    error = e instanceof Error ? e.message : String(e);
    announcer.say(`Error: ${error}`, 'assertive');
  }

  /**
   * Reports that the File System Access path was refused and the download
   * fallback took over. Not an error — the work is safe; only in-place Save is
   * unavailable for this file.
   */
  function noteFallback(reason: string | undefined): void {
    if (reason === undefined) return;
    notice = `Using the download fallback because ${reason}. Your document is fine — “Save” will offer a copy instead of writing back to the original.`;
    announcer.say(notice);
  }

  function newDocument(): void {
    if (store.dirty && !confirm('Discard unsaved changes and start a new document?')) return;
    store.newDocument('Untitled Document');
    handle = null;
    lastIntegrity = null;
    error = '';
    announcer.say('New document created.');
  }

  async function open(): Promise<void> {
    if (store.dirty && !confirm('Discard unsaved changes and open another document?')) return;
    try {
      const file = await openDocument();
      if (file === null) return;
      store.openTar(file.bytes, file.name);
      handle = file.handle;
      lastIntegrity = null;
      error = '';
      notice = '';
      noteFallback(file.fellBackBecause);
      announcer.say(`Opened ${file.name}. ${store.report.ok ? 'Valid' : 'Has validation issues'}.`);
    } catch (e) {
      fail(e);
    }
  }

  async function save(): Promise<void> {
    try {
      const bytes = store.pack();
      // Writing back can fail long after the file was opened — a revoked
      // permission must not cost the user their edit, so fall through to
      // Save-as (which itself falls through to a download).
      if (handle !== null && (await writeHandle(handle, bytes))) {
        error = '';
        announcer.say(`Saved ${store.fileName}.`);
        return;
      }
      if (handle !== null) {
        handle = null;
        noteFallback('the original file could no longer be written');
      }
      await saveAs(bytes);
    } catch (e) {
      fail(e);
    }
  }

  async function saveAs(prepacked?: Uint8Array): Promise<void> {
    try {
      const bytes = prepacked ?? store.pack();
      const result = await saveDocumentAs(bytes, store.fileName);
      if (result.cancelled) return;
      if (result.handle !== null) {
        handle = result.handle;
        store.fileName = result.handle.name;
      }
      noteFallback(result.fellBackBecause);
      announcer.say(`Saved ${store.fileName}.`);
      error = '';
    } catch (e) {
      fail(e);
    }
  }

  /**
   * Converts a foreign document into MCDF.
   *
   * The selection may hold more than one file: a Markdown or HTML document
   * names its images by relative path and a browser has no folder to look in,
   * so the user brings them along and the first importable file is the
   * document. An EPUB carries its own images and ignores the rest.
   */
  async function startImport(files: PickedFile[]): Promise<void> {
    const document_ = files.find((file) => isImportable(file.name));
    if (document_ === undefined) {
      if (files.length > 0) {
        report(
          'None of those files is one this client can import. ' +
            'Supported: Markdown (.md), HTML (.html) and EPUB (.epub).',
        );
      }
      return;
    }
    if (store.dirty && !confirm('Discard unsaved changes and import another document?')) return;

    importing = true;
    error = '';
    notice = '';
    announcer.say(`Converting ${document_.name}…`);
    try {
      const companions = files.filter((file) => file !== document_);
      pendingImport = await runImport(document_, companions);
      announcer.say(
        `Converted ${document_.name}. Review what came across, then open the document.`,
        'assertive',
      );
    } catch (e) {
      fail(e);
    } finally {
      importing = false;
    }
  }

  function acceptImport(): void {
    const outcome = pendingImport;
    if (outcome === null) return;
    pendingImport = null;
    store.adoptImported(outcome.container, outcome.fileName);
    handle = null;
    lastIntegrity = null;
    announcer.say(
      `Opened ${outcome.fileName}, converted from ${outcome.report.source}. ` +
        'It has not been saved yet.',
    );
  }

  function cancelImport(): void {
    const name = pendingImport?.report.source ?? 'the file';
    pendingImport = null;
    announcer.say(`Discarded the document converted from ${name}.`);
  }

  /**
   * Attaches a file as a container member under `assets/` and inserts a
   * reference to it. Studio has had this since day one; without it a browser
   * user can write `![x](assets/x.png)` but has no way to put the bytes in the
   * container, so the reference can never resolve.
   */
  async function attachAsset(): Promise<void> {
    try {
      const file = await openViaInput('image/*');
      if (file === null) return;

      // Member paths are relative and separator-normalised; keep the basename.
      const base = file.name.split(/[/\\]/).pop() ?? 'asset';
      const path = uniqueMemberPath(`assets/${base}`);
      store.attach(path, file.bytes);

      const alt = base.replace(/\.[^.]+$/, '');
      const snippet = `![${alt}](${path})`;
      store.content = store.content.endsWith('\n')
        ? `${store.content}\n${snippet}\n`
        : `${store.content}\n\n${snippet}\n`;

      error = '';
      announcer.say(`Attached ${path} and added a reference to it.`);
    } catch (e) {
      fail(e);
    }
  }

  /** Avoids silently replacing an existing member of the same name. */
  function uniqueMemberPath(path: string): string {
    if (!store.working.has(path)) return path;
    const dot = path.lastIndexOf('.');
    const stem = dot === -1 ? path : path.slice(0, dot);
    const ext = dot === -1 ? '' : path.slice(dot);
    for (let n = 2; ; n++) {
      const candidate = `${stem}-${n}${ext}`;
      if (!store.working.has(candidate)) return candidate;
    }
  }

  function exportCanonicalManifest(): void {
    download(
      new TextEncoder().encode(`${store.canonicalManifest}\n`),
      'manifest.json',
      'application/json',
    );
    announcer.say('Canonical manifest downloaded.');
  }

  /**
   * Drag-and-drop, routed by what was dropped.
   *
   * A `.mcdf` is opened; anything an importer recognises is converted. This is
   * the interaction the browser is best at and the reason importing belongs
   * here most of all — there is no CLI to fall back on in a tab.
   */
  async function onDrop(e: DragEvent): Promise<void> {
    e.preventDefault();
    dragging = false;
    const dropped = [...(e.dataTransfer?.files ?? [])];
    if (dropped.length === 0) return;

    try {
      const files = await Promise.all(dropped.map(readBlob));
      const container = files.find((file) => file.name.toLowerCase().endsWith('.mcdf'));
      if (container === undefined) {
        await startImport(files.map((file) => ({ name: file.name, bytes: file.bytes })));
        return;
      }

      if (store.dirty && !confirm('Discard unsaved changes and open the dropped document?')) {
        return;
      }
      store.openTar(container.bytes, container.name);
      handle = null;
      lastIntegrity = null;
      error = '';
      announcer.say(`Opened ${container.name}.`);
    } catch (err) {
      fail(err);
    }
  }

  /**
   * Moves the editor caret to a heading picked in the outline.
   *
   * The editor loads asynchronously, so a jump can be requested before it
   * exists — clicking an outline entry the moment a document opens is the
   * ordinary case, not a corner one. The target is held and applied when the
   * editor arrives rather than silently dropped.
   */
  let pendingJump = $state<number | null>(null);

  function jumpTo(text: string, line: number): void {
    if (editor === undefined) {
      pendingJump = line;
    } else {
      editor.goToLine(line);
    }
    announcer.say(`Moved to “${text}”, line ${line}.`);
  }

  $effect(() => {
    const line = pendingJump;
    const view = editor;
    if (line === null || view === undefined) return;
    pendingJump = null;
    view.goToLine(line);
  });

  function report(message: string): void {
    error = message;
    announcer.say(message, 'assertive');
  }

  let contentSealed = $derived(store.sealed.has('content.md'));
</script>

<svelte:window
  ondragover={(e) => {
    e.preventDefault();
    dragging = true;
  }}
  ondragleave={() => (dragging = false)}
  ondrop={onDrop}
/>

<a class="skip-link" href="#editor-region">Skip to the editor</a>

<div class="shell" class:dragging>
  <header class="banner">
    <div class="brand">
      <h1>MCDF Web</h1>
      <p class="tagline">Documents that carry their own proof — entirely in your browser.</p>
    </div>
    <Toolbar
      fileName={store.fileName}
      dirty={store.dirty}
      canSaveInPlace={handle !== null}
      {view}
      {theme}
      onnew={newDocument}
      onopen={() => void open()}
      onimport={() => void pickForImport().then(startImport)}
      onsave={() => void save()}
      onsaveas={() => void saveAs()}
      onviewchange={(v) => (view = v)}
      onthemechange={(t) => (theme = t)}
    />
  </header>

  {#if error !== ''}
    <p class="error" role="alert">
      {error}
      <button type="button" onclick={() => (error = '')}>Dismiss</button>
    </p>
  {/if}

  {#if notice !== ''}
    <p class="notice" role="status">
      {notice}
      <button type="button" onclick={() => (notice = '')}>Dismiss</button>
    </p>
  {/if}

  {#if importing}
    <p class="notice" role="status">Converting the file into MCDF…</p>
  {/if}

  {#if updateReady}
    <!--
      A waiting update is offered, never applied automatically: swapping the
      running app out from under an unsaved document would lose it.
    -->
    <p class="notice" role="status">
      A new version of MCDF Web is ready. Save your work first — reloading discards unsaved
      changes.
      <button type="button" onclick={applyServiceWorkerUpdate}>Reload</button>
      <button type="button" onclick={() => (updateReady = false)}>Later</button>
    </p>
  {/if}

  <div class="workspace">
    <main id="editor-region" class="main" class:split={view === 'split'}>
      <h2 class="visually-hidden">Document</h2>

      {#if view !== 'preview'}
        <section
          class="pane"
          aria-labelledby="editor-heading"
          aria-busy={!contentSealed && Editor === undefined}
        >
          <h3 id="editor-heading" class="pane-title">
            Source{contentSealed ? ' — encrypted' : ''}
          </h3>
          {#if contentSealed}
            <!--
              Not an empty editor: content.md is ciphertext, and an editable
              buffer over it would be written back on save, destroying the only
              copy. The panel that can undo this is named so the way out is
              obvious.
            -->
            <div class="sealed">
              <p>
                <strong>This document's content is encrypted.</strong>
              </p>
              <p>
                The bytes are sealed with AES-256-GCM and cannot be shown or edited until they are
                decrypted. Open the <em>Encryption</em> panel and decrypt with a recipient key to
                continue editing.
              </p>
              <p class="muted">
                Everything else still works: the manifest covers the ciphertext, so the document
                can be verified, signed and audited exactly as it is.
              </p>
            </div>
          {:else}
            <p id="editor-help" class="visually-hidden">
              CommonMark source of content.md. Headings may carry an anchor in curly braces, for
              example: hash space Title space open-brace hash overview close-brace.
            </p>
            {#if Editor !== undefined}
              <Editor
                bind:this={editor}
                bind:value={store.content}
                label="Document source, CommonMark"
                describedBy="editor-help"
              />
            {:else}
              <!--
                Not a live region. `aria-busy` on the pane is the ARIA way to
                say "this is still arriving", and a second role="status" would
                compete with the document's own status announcements for the
                few milliseconds the chunk takes.
              -->
              <p class="loading">Loading the editor…</p>
            {/if}
          {/if}
        </section>
      {/if}

      {#if view !== 'editor'}
        <section class="pane" aria-labelledby="preview-heading">
          <h3 id="preview-heading" class="pane-title">Preview</h3>
          <Preview {store} />
        </section>
      {/if}
    </main>

    <aside class="sidebar" aria-label="Document panels">
      <h2 class="visually-hidden">Panels</h2>
      <StructurePanel
        {store}
        onjump={jumpTo}
        onbound={(id, heading) =>
          announcer.say(`Section ${id} is now bound to the heading “${heading}”.`)}
      />
      <ValidationPanel {store} />
      <ManifestPanel
        {store}
        onmanifestrebuilt={() => announcer.say('Manifest rebuilt from the current bytes.')}
      />
      <TrustPanel
        {store}
        onsigned={(kid, file) => announcer.say(`Signed as ${kid}. Wrote ${file}.`)}
        onerror={report}
        onannounce={(message, assertive) =>
          announcer.say(message, assertive === true ? 'assertive' : 'polite')}
      />
      <EncryptionPanel
        {store}
        onerror={report}
        onannounce={(message, assertive) =>
          announcer.say(message, assertive === true ? 'assertive' : 'polite')}
      />
      <AuditPanel
        {store}
        onerror={report}
        onannounce={(message, assertive) =>
          announcer.say(message, assertive === true ? 'assertive' : 'polite')}
      />
      <DiffPanel {store} onerror={report} />
      <SchemaPanel
        {store}
        onadopted={(count) =>
          announcer.say(`Schema rebuilt from content: ${count} sections.`)}
      />
      <MetadataPanel {store} />
      <div class="tools">
        <button type="button" onclick={() => void attachAsset()}>Attach asset…</button>
        <button type="button" onclick={exportCanonicalManifest}>
          Export canonical manifest
        </button>
      </div>
    </aside>
  </div>

  <footer class="contentinfo">
    <p>
      Everything runs locally — no document leaves this tab.
      {#if !supportsPicker}
        This browser has no File System Access API, so saving downloads a copy.
      {/if}
      {#if !secure}
        <strong>Not a secure context</strong>: signing needs HTTPS.
      {/if}
    </p>
    <p>
      <a href="https://github.com/sukesh-ak/MCDF">MCDF project</a> ·
      <a href="https://github.com/sukesh-ak/MCDF/blob/main/conformance/GUIDE.md">
        Implementer's guide
      </a>
    </p>
  </footer>
</div>

{#if pendingImport !== null}
  <ConversionReport
    report={pendingImport.report}
    fileName={pendingImport.fileName}
    onaccept={acceptImport}
    oncancel={cancelImport}
  />
{/if}

<!--
  Two live regions, always present in the DOM: a region inserted at the moment
  it gains text is unreliable across screen readers.
-->
<div class="visually-hidden" aria-live="polite" aria-atomic="true">{announcer.polite}</div>
<div class="visually-hidden" role="alert" aria-atomic="true">{announcer.assertive}</div>

<style>
  /* The shell owns the viewport: it is exactly one screen tall and never
     scrolls. Every region inside it that can grow (the sidebar, each pane, the
     preview) scrolls on its own, so the toolbar and the footer stay put.

     `100dvh` rather than `height: 100%`, because the percentage chain only
     works while `html`, `body` and `#app` all keep their own `height: 100%` —
     three rules in another file that have to stay true for this one to hold.
     `overflow: hidden` is the guarantee rather than the mechanism: nothing
     should reach it, and if something ever does, the page still must not start
     scrolling the header off the top. */
  .shell {
    display: flex;
    flex-direction: column;
    height: 100dvh;
    overflow: hidden;
  }

  .shell.dragging {
    outline: 3px dashed var(--accent);
    outline-offset: -6px;
  }

  .banner {
    background: var(--bg-raised);
    border-bottom: 1px solid var(--border);
  }

  .brand {
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 0.75rem;
    padding: 0.6rem 0.75rem 0.35rem;
  }

  h1 {
    margin: 0;
    font-size: 1.05rem;
    letter-spacing: -0.01em;
  }

  .tagline {
    margin: 0;
    font-size: 0.82rem;
    color: var(--fg-muted);
  }

  .error {
    display: flex;
    gap: 0.75rem;
    align-items: center;
    margin: 0;
    padding: 0.5rem 0.75rem;
    background: var(--danger-bg);
    color: var(--danger);
    border-bottom: 1px solid var(--danger);
    font-size: 0.88rem;
  }

  .notice {
    display: flex;
    gap: 0.75rem;
    align-items: center;
    margin: 0;
    padding: 0.5rem 0.75rem;
    background: var(--warn-bg);
    color: var(--warn);
    border-bottom: 1px solid var(--warn);
    font-size: 0.88rem;
  }

  /* The row track has to be capped as deliberately as the columns are.
     `min-height: 0` lets the *flex item* shrink, but the grid inside it still
     lays out an implicit `auto` row, and an auto row sizes to its content — so
     a tall sidebar makes the row taller than the workspace, overflows it, and
     pushes the whole page past the viewport. The sidebar's `overflow-y: auto`
     then has nothing to scroll, because it was never the thing constrained.
     `minmax(0, 1fr)` pins the row to the container instead. */
  .workspace {
    flex: 1;
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(20rem, 26rem);
    grid-template-rows: minmax(0, 1fr);
    min-height: 0;
  }

  .main {
    display: grid;
    grid-template-columns: minmax(0, 1fr);
    grid-template-rows: minmax(0, 1fr);
    min-height: 0;
    border-right: 1px solid var(--border);
  }

  .main.split {
    grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
  }

  .pane {
    display: grid;
    grid-template-rows: auto minmax(0, 1fr);
    min-height: 0;
    border-right: 1px solid var(--border);
  }

  .pane:last-child {
    border-right: 0;
  }

  .loading {
    margin: 0;
    padding: 1rem 1.25rem;
    color: var(--fg-muted);
    font-size: 0.85rem;
  }

  .sealed {
    padding: 1rem 1.25rem;
    overflow: auto;
    max-width: 42rem;
  }

  .sealed p {
    margin: 0 0 0.6rem;
    font-size: 0.9rem;
  }

  .sealed .muted {
    color: var(--fg-muted);
    font-size: 0.82rem;
  }

  .pane-title {
    margin: 0;
    padding: 0.3rem 0.75rem;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--fg-muted);
    background: var(--bg-sunken);
    border-bottom: 1px solid var(--border);
  }

  /* `min-width: 0` is what actually stops a wide child (a long member path, a
     digest) from stretching the grid column: a grid item's default `min-width`
     is `auto`, i.e. its content, so without this the sidebar grows and the whole
     page gets a horizontal scrollbar. */
  .sidebar {
    overflow-x: hidden;
    overflow-y: auto;
    min-width: 0;
    padding: var(--gap);
    background: var(--bg-sunken);
  }

  .tools {
    display: grid;
    gap: 0.4rem;
  }

  .contentinfo {
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    gap: 0.5rem;
    padding: 0.4rem 0.75rem;
    background: var(--bg-raised);
    border-top: 1px solid var(--border);
    font-size: 0.78rem;
    color: var(--fg-muted);
  }

  .contentinfo p {
    margin: 0;
  }

  @media (max-width: 60rem) {
    /* Stacked rather than side by side, and here the page *should* scroll:
       squeezing an editor and nine panels into one narrow viewport by making
       each its own scroller is worse than one honest page scroll. So the shell
       is released from the viewport and both children size to their content. */
    .shell {
      height: auto;
      min-height: 100dvh;
      overflow: visible;
    }

    .workspace {
      grid-template-columns: minmax(0, 1fr);
      grid-template-rows: auto auto;
    }

    .main.split {
      grid-template-columns: minmax(0, 1fr);
      grid-template-rows: minmax(12rem, 1fr) minmax(12rem, 1fr);
    }

    .sidebar {
      border-top: 1px solid var(--border);
    }
  }
</style>
