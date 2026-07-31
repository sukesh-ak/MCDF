<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  Conformance: the same five profiles and the same normative error codes the
  reference CLI reports (conformance/errors.md). The codes are shown verbatim so
  a report from this client can be quoted straight into a bug.
-->
<script lang="ts">
  import { PROFILES, type Profile } from 'mcdf-ts';

  import Panel from '../components/Panel.svelte';
  import type { DocumentStore } from '../state/document.svelte.ts';

  interface Props {
    store: DocumentStore;
  }

  let { store }: Props = $props();

  const DESCRIPTIONS: Record<Profile, string> = {
    core: 'Structure only: content, schema binding, safe member paths.',
    integrity: 'Core plus manifest coverage and digests.',
    signed: 'Integrity plus detached JWS over the canonical manifest.',
    encrypted: 'Signed plus encryption policy soundness.',
    render: 'Encrypted plus reproducible canonical renders.',
  };

  /**
   * Profiles mcdf-ts checks. Render is the exception: its canonical output must
   * be byte-identical to the reference `md4c` render and there are no published
   * vectors to prove that against, so it reports E_UNIMPLEMENTED rather than
   * passing on the strength of the live preview, which is a preview.
   */
  const IMPLEMENTED = new Set<Profile>(['core', 'integrity', 'signed', 'encrypted']);

  let report = $derived(store.report);
</script>

<Panel
  title="Conformance"
  badge={report.ok ? 'valid' : `${report.issues.length} ${report.issues.length === 1 ? 'issue' : 'issues'}`}
  tone={report.ok ? 'ok' : 'danger'}
>
  <p class="field">
    <label for="profile">Profile</label>
    <select id="profile" bind:value={store.profile}>
      {#each PROFILES as profile (profile)}
        <option value={profile}>
          {profile}{IMPLEMENTED.has(profile) ? '' : ' (not implemented yet)'}
        </option>
      {/each}
    </select>
  </p>
  <p class="description">{DESCRIPTIONS[store.profile]}</p>

  <!--
    The result is the panel's whole point, so it is a live region: a document
    that breaks while you type must be announced, not merely recoloured.
  -->
  <div aria-live="polite" aria-atomic="true">
    {#if report.ok}
      <p class="verdict ok">
        <span aria-hidden="true">✓</span>
        Valid at the {report.profile} profile.
      </p>
    {:else}
      <p class="verdict bad">
        <span aria-hidden="true">✕</span>
        {report.issues.length}
        {report.issues.length === 1 ? 'issue' : 'issues'} at the {report.profile} profile.
      </p>
      <ul class="issues">
        {#each report.issues as issue, i (`${issue.code}-${i}`)}
          <li>
            <code class="code">{issue.code}</code>
            <span class="message">{issue.message}</span>
          </li>
        {/each}
      </ul>
    {/if}
  </div>
</Panel>

<style>
  .field {
    display: grid;
    gap: 0.15rem;
    margin: 0 0 0.35rem;
  }

  .field label {
    font-size: 0.8rem;
    color: var(--fg-muted);
  }

  .description {
    margin: 0 0 0.6rem;
    font-size: 0.8rem;
    color: var(--fg-muted);
  }

  .verdict {
    margin: 0;
    font-weight: 600;
    font-size: 0.9rem;
  }

  .verdict.ok {
    color: var(--ok);
  }

  .verdict.bad {
    color: var(--danger);
  }

  .issues {
    list-style: none;
    margin: 0.5rem 0 0;
    padding: 0;
    display: grid;
    gap: 0.4rem;
  }

  .issues li {
    display: grid;
    gap: 0.1rem;
    padding: 0.35rem 0.45rem;
    background: var(--danger-bg);
    border: 1px solid var(--danger);
    border-radius: var(--radius);
  }

  .code {
    font-family: var(--font-mono);
    font-size: 0.75rem;
    color: var(--danger);
    font-weight: 700;
  }

  .message {
    font-size: 0.82rem;
  }
</style>
