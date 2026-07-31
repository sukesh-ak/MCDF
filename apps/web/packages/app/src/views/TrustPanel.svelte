<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Trust: the keyring, the signatures on this document, and the demo the whole
  format exists to make obvious — edit a signed document and the signature goes
  red while you type, then re-sign and it comes back.

  Verdicts are announced through the live region, not merely recoloured: a
  signature breaking is the single most important thing this app can tell
  someone, and colour is not a channel everyone has.
-->
<script lang="ts">
  import type { SignatureCheck } from 'mcdf-ts';

  import Panel from '../components/Panel.svelte';
  import { exportPem, shortDid, toSigningKey, type KeyAlgorithm, type StoredKey } from '../platform/keystore.ts';
  import { download, openViaInput } from '../platform/files.ts';
  import type { DocumentStore } from '../state/document.svelte.ts';
  import { keyring } from '../state/keyring.svelte.ts';

  interface Props {
    store: DocumentStore;
    onsigned?: (kid: string, file: string) => void;
    onerror?: (message: string) => void;
    onannounce?: (message: string, assertive?: boolean) => void;
  }

  let { store, onsigned, onerror, onannounce }: Props = $props();

  const ALGORITHM_LABELS: Record<KeyAlgorithm, string> = {
    EdDSA: 'Ed25519 (EdDSA)',
    ES256: 'NIST P-256 (ES256)',
    X25519: 'X25519 (recipient)',
  };

  let selectedDid = $state('');
  let signatureName = $state('author');
  let newKeyAlgorithm = $state<KeyAlgorithm>('EdDSA');
  let newKeyLabel = $state('');
  let busy = $state(false);

  let keys = $derived(keyring.keys);
  let signingKeys = $derived(keyring.signing);
  let selected = $derived(signingKeys.find((k) => k.did === selectedDid));
  let signatures = $derived(store.signatures);
  let broken = $derived(signatures.filter((s) => !s.valid));

  // Keep the selection pointing at a key that still exists.
  $effect(() => {
    if (selectedDid === '' || !signingKeys.some((k) => k.did === selectedDid)) {
      selectedDid = signingKeys[0]?.did ?? '';
    }
  });

  async function guard(what: string, body: () => Promise<void> | void): Promise<void> {
    busy = true;
    try {
      await body();
    } catch (e) {
      onerror?.(`${what}: ${(e as Error).message}`);
    } finally {
      busy = false;
    }
  }

  function generate(): void {
    void guard('Generate key', async () => {
      const label = newKeyLabel.trim() === '' ? defaultLabel(newKeyAlgorithm) : newKeyLabel.trim();
      const key = await keyring.generate(newKeyAlgorithm, label);
      newKeyLabel = '';
      selectedDid = key.purpose === 'signing' ? key.did : selectedDid;
      onannounce?.(`Generated ${ALGORITHM_LABELS[key.algorithm]} key ${label}.`);
    });
  }

  function defaultLabel(algorithm: KeyAlgorithm): string {
    const existing = keys.filter((k) => k.algorithm === algorithm).length + 1;
    return `${algorithm === 'X25519' ? 'Recipient' : 'Signing'} key ${existing}`;
  }

  function importKey(): void {
    void guard('Import key', async () => {
      const file = await openViaInput('.pem,.key');
      if (file === null) return;
      const label = file.name.replace(/\.(pem|key)$/i, '');
      const key = await keyring.import(new TextDecoder().decode(file.bytes), label);
      onannounce?.(`Imported ${label} (${key.did}).`);
    });
  }

  function exportKey(key: StoredKey): void {
    void guard('Export key', () => {
      download(
        new TextEncoder().encode(exportPem(key)),
        `${key.label.replace(/[^\w.-]+/g, '-')}.pem`,
        'application/x-pem-file',
      );
      onannounce?.(`Exported ${key.label} as a PKCS#8 PEM.`);
    });
  }

  function forget(key: StoredKey): void {
    if (!confirm(`Delete “${key.label}”? Without a PEM export this key cannot be recovered.`))
      return;
    void guard('Delete key', async () => {
      await keyring.remove(key.did);
      onannounce?.(`Deleted ${key.label}.`, true);
    });
  }

  function sign(): void {
    const key = selected;
    if (key === undefined) return;
    const name = signatureName.trim() === '' ? 'author' : signatureName.trim();
    void guard('Sign', () => {
      const check = store.sign(toSigningKey(key), name);
      onsigned?.(check.kid, check.file);
    });
  }

  function unsign(check: SignatureCheck): void {
    if (!confirm(`Remove ${check.file}?`)) return;
    store.removeSignature(check.file);
    onannounce?.(`Removed ${check.file}.`, true);
  }

  let badge = $derived(
    signatures.length === 0
      ? 'unsigned'
      : broken.length === 0
        ? `${signatures.length} valid`
        : `${broken.length} broken`,
  );
</script>

<Panel
  title="Trust"
  {badge}
  tone={signatures.length === 0 ? 'neutral' : broken.length === 0 ? 'ok' : 'danger'}
>
  <!--
    Signature state first: it is what someone opening this panel wants to know,
    and it is a live region so a break during typing is announced.
  -->
  <div aria-live="polite" aria-atomic="true">
    {#if signatures.length === 0}
      <p class="empty">
        Not signed. A signature covers the <em>canonical manifest</em>, so it commits to every
        byte of every member at once.
      </p>
    {:else}
      <ul class="signatures">
        {#each signatures as check (check.file)}
          <li class={check.valid ? 'valid' : 'invalid'}>
            <p class="verdict">
              <span aria-hidden="true">{check.valid ? '✓' : '✕'}</span>
              <code>{check.file}</code>
              <strong>{check.valid ? 'valid' : 'invalid'}</strong>
              {#if check.alg !== ''}<span class="alg">{check.alg}</span>{/if}
            </p>
            {#if check.kid !== ''}
              <p class="kid" title={check.kid}>Signer <code>{shortDid(check.kid)}</code></p>
            {/if}
            {#if !check.valid}
              <p class="why">
                {check.error === ''
                  ? 'Does not verify over the manifest as it would be saved — the document changed after signing.'
                  : check.error}
              </p>
            {/if}
            <button type="button" class="link" onclick={() => unsign(check)}>Remove</button>
          </li>
        {/each}
      </ul>
      {#if broken.length > 0}
        <p class="repair">
          Nothing is lost: sign again below and the document is trustworthy once more. This is
          the guarantee — a signed document cannot change quietly.
        </p>
      {/if}
    {/if}
  </div>

  <h4>Sign</h4>
  {#if keyring.error !== ''}
    <p class="warn">{keyring.error}</p>
  {/if}
  {#if signingKeys.length === 0}
    <p class="empty">No signing key yet. Generate or import one below.</p>
  {:else}
    <div class="row">
      <p class="field">
        <label for="signing-key">Key</label>
        <select id="signing-key" bind:value={selectedDid}>
          {#each signingKeys as key (key.did)}
            <option value={key.did}>{key.label} · {ALGORITHM_LABELS[key.algorithm]}</option>
          {/each}
        </select>
      </p>
      <p class="field">
        <label for="signature-name">File name</label>
        <input
          id="signature-name"
          type="text"
          bind:value={signatureName}
          aria-describedby="signature-name-help"
        />
      </p>
    </div>
    <p id="signature-name-help" class="hint">
      Written to <code>signatures/{signatureName.trim() === '' ? 'author' : signatureName.trim()}.sig</code>.
      Several people can sign the same document, each under their own name.
    </p>
    <button type="button" onclick={sign} disabled={busy || selected === undefined}>
      {signatures.some((s) => !s.valid) ? 'Re-sign' : 'Sign'} the manifest
    </button>
    <p class="hint">Signing rebuilds the manifest first, so it always covers what you see now.</p>
  {/if}

  <h4>Keys</h4>
  <p class="hint privacy">
    Keys are stored in this browser only and never sent anywhere. IndexedDB is not a hardware
    token: anything running on this origin can read them. For a key that matters, keep the PEM
    offline and import it when you need it.
  </p>

  {#if keys.length > 0}
    <ul class="keys">
      {#each keys as key (key.did)}
        <li>
          <p class="key-line">
            <span class="key-label">{key.label}</span>
            <span class="key-alg">{ALGORITHM_LABELS[key.algorithm]}</span>
          </p>
          <p class="kid" title={key.did}><code>{shortDid(key.did)}</code></p>
          <p class="key-actions">
            <button type="button" class="link" onclick={() => exportKey(key)}>Export PEM</button>
            <button type="button" class="link danger" onclick={() => forget(key)}>Delete</button>
          </p>
        </li>
      {/each}
    </ul>
  {/if}

  <div class="row">
    <p class="field">
      <label for="key-algorithm">New key type</label>
      <select id="key-algorithm" bind:value={newKeyAlgorithm}>
        <option value="EdDSA">{ALGORITHM_LABELS.EdDSA}</option>
        <option value="ES256">{ALGORITHM_LABELS.ES256}</option>
        <option value="X25519">{ALGORITHM_LABELS.X25519}</option>
      </select>
    </p>
    <p class="field">
      <label for="key-label">Label</label>
      <input id="key-label" type="text" bind:value={newKeyLabel} placeholder={defaultLabel(newKeyAlgorithm)} />
    </p>
  </div>
  <p class="key-actions">
    <button type="button" onclick={generate} disabled={busy || !keyring.available}>Generate</button>
    <button type="button" onclick={importKey} disabled={busy || !keyring.available}>Import PEM…</button>
  </p>
  <p class="hint">
    The same PKCS#8 PEM the <code>mcdf</code> and <code>mcdf-ts</code> command-line tools read
    and write, so a key works in all three.
  </p>
</Panel>

<style>
  h4 {
    margin: 0.9rem 0 0.35rem;
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--fg-muted);
  }

  .empty,
  .hint {
    margin: 0.25rem 0 0.5rem;
    font-size: 0.8rem;
    color: var(--fg-muted);
  }

  .privacy {
    border-left: 2px solid var(--border-strong);
    padding-left: 0.5rem;
  }

  .warn {
    margin: 0.25rem 0;
    font-size: 0.8rem;
    color: var(--warn);
  }

  .signatures,
  .keys {
    list-style: none;
    margin: 0.25rem 0 0.5rem;
    padding: 0;
    display: grid;
    gap: 0.4rem;
  }

  .signatures li,
  .keys li {
    padding: 0.4rem 0.5rem;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    min-width: 0;
  }

  .signatures li.valid {
    border-color: var(--ok);
    background: var(--ok-bg);
  }

  .signatures li.invalid {
    border-color: var(--danger);
    background: var(--danger-bg);
  }

  .verdict {
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 0.35rem;
    margin: 0;
    font-size: 0.82rem;
  }

  .signatures li.valid .verdict strong {
    color: var(--ok);
  }

  .signatures li.invalid .verdict strong {
    color: var(--danger);
  }

  .alg {
    font-size: 0.72rem;
    color: var(--fg-muted);
  }

  .kid,
  .why {
    margin: 0.15rem 0 0;
    font-size: 0.75rem;
    color: var(--fg-muted);
    overflow-wrap: anywhere;
  }

  .why {
    color: var(--danger);
  }

  .repair {
    margin: 0.4rem 0 0;
    font-size: 0.8rem;
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

  .key-line {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    align-items: baseline;
    margin: 0;
    font-size: 0.82rem;
  }

  .key-label {
    font-weight: 600;
  }

  .key-alg {
    font-size: 0.72rem;
    color: var(--fg-muted);
  }

  .key-actions {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    margin: 0.3rem 0 0;
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

  .link.danger {
    color: var(--danger);
  }
</style>
