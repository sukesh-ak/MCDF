<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Confidentiality: AES-256-GCM over chosen members, with the content key wrapped
  to each recipient by HPKE. Recipients are named by their X25519 did:key, so
  encrypting to someone needs their identifier and nothing else — no server, no
  key exchange, no account.
-->
<script lang="ts">
  import Panel from '../components/Panel.svelte';
  import { shortDid, toRecipientKey } from '../platform/keystore.ts';
  import type { DocumentStore } from '../state/document.svelte.ts';
  import { keyring } from '../state/keyring.svelte.ts';

  interface Props {
    store: DocumentStore;
    onerror?: (message: string) => void;
    onannounce?: (message: string, assertive?: boolean) => void;
  }

  let { store, onerror, onannounce }: Props = $props();

  let recipientDids = $state<string[]>([]);
  let extraRecipient = $state('');
  let chosenFiles = $state<string[]>([]);
  let decryptWith = $state('');
  let busy = $state(false);

  let recipientKeys = $derived(keyring.recipients);
  let policy = $derived(store.policy);
  /** Recipient keys held here that the document is actually addressed to. */
  let usable = $derived(
    recipientKeys.filter((k) => policy?.recipients.some((r) => r.id === k.did) ?? false),
  );

  // Default to encrypting content.md; assets are opt-in per document.
  $effect(() => {
    if (chosenFiles.length === 0 && store.encryptableMembers.includes('content.md')) {
      chosenFiles = ['content.md'];
    }
  });

  $effect(() => {
    if (decryptWith === '' || !usable.some((k) => k.did === decryptWith)) {
      decryptWith = usable[0]?.did ?? '';
    }
  });

  async function guard(what: string, body: () => Promise<void>): Promise<void> {
    busy = true;
    try {
      await body();
    } catch (e) {
      onerror?.(`${what}: ${(e as Error).message}`);
    } finally {
      busy = false;
    }
  }

  function toggleRecipient(did: string, on: boolean): void {
    recipientDids = on ? [...new Set([...recipientDids, did])] : recipientDids.filter((d) => d !== did);
  }

  function toggleFile(path: string, on: boolean): void {
    chosenFiles = on ? [...new Set([...chosenFiles, path])] : chosenFiles.filter((p) => p !== path);
  }

  function addTypedRecipient(): void {
    const did = extraRecipient.trim();
    if (did === '') return;
    recipientDids = [...new Set([...recipientDids, did])];
    extraRecipient = '';
  }

  function encrypt(): void {
    void guard('Encrypt', async () => {
      await store.encrypt(chosenFiles, recipientDids);
      onannounce?.(
        `Encrypted ${chosenFiles.length} member(s) for ${recipientDids.length} recipient(s). ` +
          'Any signature made before now no longer verifies.',
        true,
      );
      recipientDids = [];
    });
  }

  function decrypt(): void {
    const key = keyring.find(decryptWith);
    if (key === undefined) return;
    void guard('Decrypt', async () => {
      const files = await store.decrypt(toRecipientKey(key));
      onannounce?.(`Decrypted ${files.length} member(s). The document is editable again.`, true);
    });
  }
</script>

<Panel
  title="Encryption"
  badge={store.encrypted ? `${policy?.recipients.length ?? 0} recipients` : 'plaintext'}
  tone={store.encrypted ? 'ok' : 'neutral'}
  open={false}
>
  {#if store.encrypted && policy !== null}
    <p class="state">
      <strong>Encrypted.</strong>
      {policy.method} with the content key wrapped by {policy.key_management}.
    </p>
    <dl class="summary">
      <dt>Sealed members</dt>
      <dd>
        <ul class="plain">
          {#each policy.encrypted_files as file (file)}<li><code>{file}</code></li>{/each}
        </ul>
      </dd>
      <dt>Recipients</dt>
      <dd>
        <ul class="plain">
          {#each policy.recipients as recipient (recipient.id)}
            <li title={recipient.id}>
              <code>{shortDid(recipient.id)}</code>
              {#if recipientKeys.some((k) => k.did === recipient.id)}
                <span class="you">you</span>
              {/if}
            </li>
          {/each}
        </ul>
      </dd>
      {#if policy.structure.present}
        <dt>Structure</dt>
        <dd>
          {#if policy.structure.bound_sections.length === 0}
            no section bound when this was sealed
          {:else}
            <ul class="plain">
              {#each policy.structure.bound_sections as id (id)}<li><code>{id}</code></li>{/each}
            </ul>
          {/if}
        </dd>
      {/if}
    </dl>

    {#if policy.structure.present}
      <p class="hint">
        The sections above bound to a heading when the content was sealed, so the schema can
        still be checked while the document is closed. It is the encryptor's word, not a proof —
        it is covered by the signature, and decrypting re-checks it against the real headings.
      </p>
    {/if}

    <p class="hint">
      The editor is read-only while sealed — writing a text buffer over ciphertext would destroy
      it. The manifest hashes the ciphertext, so Integrity still holds and the document can be
      signed in this state.
    </p>

    {#if usable.length === 0}
      <p class="warn">
        None of the keys in this browser is a recipient of this document. Import the matching
        X25519 PEM in the Trust panel to open it.
      </p>
    {:else}
      <p class="field">
        <label for="decrypt-key">Open with</label>
        <select id="decrypt-key" bind:value={decryptWith}>
          {#each usable as key (key.did)}<option value={key.did}>{key.label}</option>{/each}
        </select>
      </p>
      <button type="button" onclick={decrypt} disabled={busy}>Decrypt</button>
    {/if}
  {:else}
    <p class="hint">
      Encrypting seals the chosen members with AES-256-GCM under one content key, then wraps that
      key separately for each recipient. Add or remove a recipient later by decrypting and
      encrypting again — the content key is never reused across policies.
    </p>

    <fieldset>
      <legend>Members to encrypt</legend>
      {#each store.encryptableMembers as path (path)}
        <label class="check">
          <input
            type="checkbox"
            checked={chosenFiles.includes(path)}
            onchange={(e) => toggleFile(path, e.currentTarget.checked)}
          />
          <code>{path}</code>
        </label>
      {/each}
      <p class="hint">
        <code>metadata.yaml</code> and <code>schema.yaml</code> are not offered: the panels that
        read them would go blank, and validation would report a required section as
        <em>missing</em> when it is only unreadable.
      </p>
    </fieldset>

    <fieldset>
      <legend>Recipients</legend>
      {#if recipientKeys.length === 0}
        <p class="hint">No X25519 key in this browser yet — generate one in the Trust panel.</p>
      {:else}
        {#each recipientKeys as key (key.did)}
          <label class="check">
            <input
              type="checkbox"
              checked={recipientDids.includes(key.did)}
              onchange={(e) => toggleRecipient(key.did, e.currentTarget.checked)}
            />
            {key.label} <code class="kid">{shortDid(key.did)}</code>
          </label>
        {/each}
      {/if}

      <p class="field">
        <label for="extra-recipient">Someone else's did:key</label>
        <input
          id="extra-recipient"
          type="text"
          bind:value={extraRecipient}
          placeholder="did:key:z6LS…"
          onkeydown={(e) => {
            if (e.key === 'Enter') {
              e.preventDefault();
              addTypedRecipient();
            }
          }}
        />
      </p>
      <button type="button" class="secondary" onclick={addTypedRecipient} disabled={extraRecipient.trim() === ''}>
        Add recipient
      </button>

      {#if recipientDids.length > 0}
        <ul class="plain chosen">
          {#each recipientDids as did (did)}
            <li>
              <code title={did}>{shortDid(did)}</code>
              <button type="button" class="link" onclick={() => toggleRecipient(did, false)}>
                Remove
              </button>
            </li>
          {/each}
        </ul>
      {/if}
    </fieldset>

    <button
      type="button"
      onclick={encrypt}
      disabled={busy || chosenFiles.length === 0 || recipientDids.length === 0}
    >
      Encrypt {chosenFiles.length} member{chosenFiles.length === 1 ? '' : 's'}
    </button>
    <p class="hint">
      Encrypting changes the bytes, so it changes the manifest, so any existing signature stops
      verifying. Sign after encrypting, not before.
    </p>
  {/if}
</Panel>

<style>
  .state {
    margin: 0.25rem 0 0.5rem;
    font-size: 0.85rem;
  }

  .hint {
    margin: 0.25rem 0 0.5rem;
    font-size: 0.78rem;
    color: var(--fg-muted);
  }

  .warn {
    margin: 0.4rem 0;
    font-size: 0.8rem;
    color: var(--warn);
  }

  .summary {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr);
    gap: 0.15rem 0.6rem;
    margin: 0 0 0.5rem;
    font-size: 0.8rem;
  }

  .summary dt {
    color: var(--fg-muted);
  }

  .summary dd {
    margin: 0;
    min-width: 0;
  }

  .plain {
    list-style: none;
    margin: 0;
    padding: 0;
  }

  .plain li {
    overflow-wrap: anywhere;
  }

  .chosen {
    margin-top: 0.35rem;
    display: grid;
    gap: 0.2rem;
  }

  .you {
    font-size: 0.7rem;
    color: var(--ok);
    border: 1px solid currentColor;
    border-radius: 999px;
    padding: 0 0.35rem;
  }

  fieldset {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.4rem 0.6rem 0.6rem;
    margin: 0 0 0.6rem;
    min-width: 0;
  }

  legend {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    color: var(--fg-muted);
    padding: 0 0.25rem;
  }

  .check {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.82rem;
    min-height: var(--target);
    overflow-wrap: anywhere;
  }

  .kid {
    font-size: 0.72rem;
    color: var(--fg-muted);
  }

  .field {
    display: grid;
    gap: 0.15rem;
    margin: 0.4rem 0 0.35rem;
    min-width: 0;
  }

  .field label {
    font-size: 0.78rem;
    color: var(--fg-muted);
  }

  .link {
    background: none;
    border: 0;
    padding: 0.15rem 0.2rem;
    min-height: auto;
    font-size: 0.75rem;
    color: var(--accent);
    text-decoration: underline;
    cursor: pointer;
  }
</style>
