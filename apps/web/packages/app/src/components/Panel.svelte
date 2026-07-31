<!--
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

  A collapsible panel built on <details>/<summary>. Native disclosure semantics
  come with keyboard operation and correct expanded/collapsed announcements for
  free — a hand-rolled accordion would have to re-earn all of it.
-->
<script lang="ts">
  import type { Snippet } from 'svelte';

  interface Props {
    title: string;
    /** Short status shown next to the title, e.g. a count or "3 issues". */
    badge?: string;
    /** Applies the warning/danger tint to the badge. */
    tone?: 'neutral' | 'ok' | 'warn' | 'danger';
    open?: boolean;
    children: Snippet;
  }

  let { title, badge, tone = 'neutral', open = $bindable(true), children }: Props = $props();
</script>

<details class="panel" bind:open>
  <summary>
    <span class="title">{title}</span>
    {#if badge !== undefined}
      <span class="badge {tone}">{badge}</span>
    {/if}
  </summary>
  <div class="body">
    {@render children()}
  </div>
</details>

<style>
  .panel {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-raised);
    margin-bottom: var(--gap);
  }

  summary {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.5rem 0.75rem;
    min-height: var(--target);
    cursor: pointer;
    font-weight: 600;
    border-radius: var(--radius);
  }

  summary:hover {
    background: var(--bg-sunken);
  }

  .title {
    flex: 1;
  }

  .badge {
    font-weight: 500;
    font-size: 0.8rem;
    padding: 0.05rem 0.45rem;
    border-radius: 999px;
    border: 1px solid var(--border-strong);
    color: var(--fg-muted);
    white-space: nowrap;
  }

  .badge.ok {
    color: var(--ok);
    background: var(--ok-bg);
    border-color: currentColor;
  }

  .badge.warn {
    color: var(--warn);
    background: var(--warn-bg);
    border-color: currentColor;
  }

  .badge.danger {
    color: var(--danger);
    background: var(--danger-bg);
    border-color: currentColor;
  }

  .body {
    padding: 0 0.75rem 0.75rem;
    min-width: 0;
  }
</style>
