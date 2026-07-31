<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Audit: the chained history of what happened to this document.

  Each entry records the hash of the previous line, so edits and reordering are
  detectable. Plain truncation of the tail is not — a shortened chain is still a
  valid chain — which is what the signed checkpoint is for: it commits to a head
  that removed entries can no longer produce.
-->
<script lang="ts">
  import Panel from '../components/Panel.svelte';
  import { shortDid, toSigningKey } from '../platform/keystore.ts';
  import type { DocumentStore } from '../state/document.svelte.ts';
  import { keyring } from '../state/keyring.svelte.ts';

  interface Props {
    store: DocumentStore;
    onerror?: (message: string) => void;
    onannounce?: (message: string, assertive?: boolean) => void;
  }

  let { store, onerror, onannounce }: Props = $props();

  /** The actions the reference implementation writes; free text is allowed too. */
  const COMMON_ACTIONS = ['CREATED', 'EDITED', 'REVIEWED', 'SIGNED', 'ENCRYPTED', 'PUBLISHED'];

  let action = $state('EDITED');
  let actor = $state('');
  let checkpointWith = $state('');
  let busy = $state(false);

  let signingKeys = $derived(keyring.signing);
  let entries = $derived(store.auditEntries);
  let chain = $derived(store.auditState);
  let checkpoint = $derived(store.checkpoint);

  $effect(() => {
    if (checkpointWith === '' || !signingKeys.some((k) => k.did === checkpointWith)) {
      checkpointWith = signingKeys[0]?.did ?? '';
    }
  });

  // Default the actor to the first signing identity: an audit entry attributed
  // to "unknown" is barely worth writing.
  $effect(() => {
    if (actor === '' && signingKeys.length > 0) actor = signingKeys[0]?.did ?? '';
  });

  function guard(what: string, body: () => void): void {
    busy = true;
    try {
      body();
    } catch (e) {
      onerror?.(`${what}: ${(e as Error).message}`);
    } finally {
      busy = false;
    }
  }

  function append(): void {
    const text = action.trim();
    if (text === '') return;
    guard('Append', () => {
      store.appendAudit(text, actor.trim() === '' ? 'unknown' : actor.trim());
      onannounce?.(`Appended ${text} to the audit log.`);
    });
  }

  function writeCheckpoint(): void {
    const key = keyring.find(checkpointWith);
    if (key === undefined) return;
    guard('Checkpoint', () => {
      store.writeCheckpoint(toSigningKey(key));
      onannounce?.('Checkpoint written over the current log head.');
    });
  }

  function badgeTone(): 'neutral' | 'ok' | 'warn' | 'danger' {
    if (entries.length === 0) return 'neutral';
    if (!chain.ok) return 'danger';
    if (checkpoint.present && !checkpoint.valid) return 'danger';
    return 'ok';
  }
</script>

<Panel
  title="Audit"
  badge={entries.length === 0 ? 'empty' : `${entries.length} ${entries.length === 1 ? 'entry' : 'entries'}`}
  tone={badgeTone()}
  open={false}
>
  <div aria-live="polite" aria-atomic="true">
    {#if entries.length === 0}
      <p class="hint">
        No history yet. The log lives outside the manifest, so it can grow after a document is
        signed without invalidating the signature.
      </p>
    {:else}
      <ol class="timeline">
        {#each entries as entry, i (`${entry.timestamp}-${i}`)}
          <li class={!chain.ok && chain.brokenAt === i ? 'broken' : ''}>
            <p class="line">
              <span class="action">{entry.action}</span>
              <time>{entry.timestamp}</time>
            </p>
            <p class="actor" title={entry.actor}>
              {entry.actor.startsWith('did:key:') ? shortDid(entry.actor) : entry.actor}
            </p>
            {#if !chain.ok && chain.brokenAt === i}
              <p class="why">Chain breaks here — this entry does not follow the one above it.</p>
            {/if}
          </li>
        {/each}
      </ol>

      <p class="verdict {chain.ok ? 'ok' : 'bad'}">
        <span aria-hidden="true">{chain.ok ? '✓' : '✕'}</span>
        {chain.ok ? `Chain intact from genesis (${chain.entries} entries).` : chain.error}
      </p>
    {/if}

    {#if checkpoint.present}
      <p class="verdict {checkpoint.valid ? 'ok' : 'bad'}">
        <span aria-hidden="true">{checkpoint.valid ? '✓' : '✕'}</span>
        Checkpoint {checkpoint.valid ? 'valid' : 'invalid'}
        {#if checkpoint.kid !== ''}<span class="kid" title={checkpoint.kid}>
            signed by {shortDid(checkpoint.kid)}</span
          >{/if}
      </p>
      {#if !checkpoint.valid && checkpoint.error !== ''}
        <p class="why">{checkpoint.error}</p>
      {/if}
    {/if}
  </div>

  <h4>Append an entry</h4>
  <div class="row">
    <p class="field">
      <label for="audit-action">Action</label>
      <input id="audit-action" type="text" bind:value={action} list="audit-actions" />
      <datalist id="audit-actions">
        {#each COMMON_ACTIONS as a (a)}<option value={a}></option>{/each}
      </datalist>
    </p>
    <p class="field">
      <label for="audit-actor">Actor</label>
      <input id="audit-actor" type="text" bind:value={actor} placeholder="name or did:key" />
    </p>
  </div>
  <button type="button" onclick={append} disabled={busy || action.trim() === ''}>Append</button>

  <h4>Checkpoint</h4>
  {#if signingKeys.length === 0}
    <p class="hint">A signing key is needed to checkpoint. Generate one in the Trust panel.</p>
  {:else}
    <p class="hint">
      Signs the current head. Entries can still be added afterwards; what becomes detectable is
      anything being <em>removed</em>.
    </p>
    <p class="field">
      <label for="checkpoint-key">Sign with</label>
      <select id="checkpoint-key" bind:value={checkpointWith}>
        {#each signingKeys as key (key.did)}<option value={key.did}>{key.label}</option>{/each}
      </select>
    </p>
    <button type="button" onclick={writeCheckpoint} disabled={busy}>
      {checkpoint.present ? 'Re-checkpoint' : 'Write checkpoint'}
    </button>
  {/if}
</Panel>

<style>
  h4 {
    margin: 0.9rem 0 0.35rem;
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--fg-muted);
  }

  .hint {
    margin: 0.25rem 0 0.5rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }

  .timeline {
    list-style: none;
    margin: 0.25rem 0 0.5rem;
    padding: 0;
    display: grid;
    gap: 0.3rem;
  }

  .timeline li {
    padding: 0.3rem 0.45rem;
    border-left: 2px solid var(--border-strong);
    min-width: 0;
  }

  .timeline li.broken {
    border-left-color: var(--danger);
    background: var(--danger-bg);
  }

  .line {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    align-items: baseline;
    margin: 0;
    font-size: 0.82rem;
  }

  .action {
    font-weight: 600;
  }

  time {
    font-size: 0.72rem;
    color: var(--fg-muted);
    font-variant-numeric: tabular-nums;
  }

  .actor,
  .kid {
    font-size: 0.75rem;
    color: var(--fg-muted);
    overflow-wrap: anywhere;
  }

  .actor {
    margin: 0.1rem 0 0;
  }

  .verdict {
    margin: 0.4rem 0 0;
    font-size: 0.82rem;
    font-weight: 600;
  }

  .verdict.ok {
    color: var(--ok);
  }

  .verdict.bad {
    color: var(--danger);
  }

  .why {
    margin: 0.15rem 0 0;
    font-size: 0.75rem;
    color: var(--danger);
  }

  .row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
    gap: 0.5rem;
  }

  .field {
    display: grid;
    gap: 0.15rem;
    margin: 0 0 0.35rem;
    min-width: 0;
  }

  .field label {
    font-size: 0.78rem;
    color: var(--fg-muted);
  }
</style>
