// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import { describe, expect, it } from 'vitest';

import { MemoryContainer } from '../container/container.js';
import { SigningKey } from '../crypto/keys.js';
import {
  AUDIT_CHECKPOINT_PATH,
  AUDIT_GENESIS,
  AUDIT_LOG_PATH,
  auditEntryToLine,
  parseAuditLog,
} from '../serialize/audit-log.js';
import {
  auditAppend,
  auditCheckpoint,
  auditHead,
  auditVerify,
  auditVerifyCheckpoint,
  readAuditLog,
} from './audit.js';

/** Fixed timestamps: entry hashes must be reproducible across runs. */
const T = (n: number): string => `2026-07-30T09:0${n}:00Z`;

function chained(actions: string[]): MemoryContainer {
  const container = new MemoryContainer();
  actions.forEach((action, i) => auditAppend(container, action, 'tester', T(i)));
  return container;
}

describe('audit.log line format', () => {
  it('is four tab-separated fields', () => {
    const line = auditEntryToLine({
      timestamp: '2026-07-30T09:00:00Z',
      action: 'CREATED',
      actor: 'tester',
      prev_hash: AUDIT_GENESIS,
    });
    expect(line).toBe(`2026-07-30T09:00:00Z\tCREATED\ttester\t${AUDIT_GENESIS}`);
    expect(parseAuditLog(`${line}\n`)).toHaveLength(1);
  });

  it('rejects a line with the wrong field count rather than skipping it', () => {
    // Skipping would silently shorten the chain and hide the break.
    expect(() => parseAuditLog('too\tfew\tfields\n')).toThrow(/malformed audit\.log/);
    expect(() => parseAuditLog('a\tb\tc\td\te\n')).toThrow(/malformed audit\.log/);
  });

  it('reads an absent log as empty, not as an error', () => {
    expect(readAuditLog(new MemoryContainer())).toEqual([]);
    expect(auditHead(new MemoryContainer())).toBe(AUDIT_GENESIS);
  });
});

describe('the hash chain', () => {
  it('chains the first entry to genesis and each one after to its predecessor', () => {
    const container = chained(['CREATED', 'SIGNED', 'ENCRYPTED']);
    const entries = readAuditLog(container);

    expect(entries).toHaveLength(3);
    expect(entries[0]?.prev_hash).toBe(AUDIT_GENESIS);
    expect(entries[1]?.prev_hash).not.toBe(AUDIT_GENESIS);
    expect(auditVerify(container)).toMatchObject({ ok: true, entries: 3 });
  });

  it('detects an edited entry', () => {
    const container = chained(['CREATED', 'SIGNED', 'ENCRYPTED']);
    const lines = container.readText(AUDIT_LOG_PATH).split('\n');
    lines[0] = (lines[0] as string).replace('CREATED', 'FORGED');
    container.writeText(AUDIT_LOG_PATH, lines.join('\n'));

    // Entry 0 still chains to genesis; entry 1's recorded prev_hash no longer
    // matches the rewritten line, so the break surfaces one entry later.
    expect(auditVerify(container)).toMatchObject({ ok: false, brokenAt: 1 });
  });

  it('detects a reordering', () => {
    const container = chained(['CREATED', 'SIGNED', 'ENCRYPTED']);
    const lines = container.readText(AUDIT_LOG_PATH).trimEnd().split('\n');
    container.writeText(AUDIT_LOG_PATH, `${[lines[1], lines[0], lines[2]].join('\n')}\n`);
    expect(auditVerify(container).ok).toBe(false);
  });

  it('does not, by itself, detect the tail being cut off', () => {
    // A truncated prefix is still a valid chain — which is precisely why
    // audit.checkpoint exists.
    const container = chained(['CREATED', 'SIGNED', 'ENCRYPTED']);
    const lines = container.readText(AUDIT_LOG_PATH).trimEnd().split('\n');
    container.writeText(AUDIT_LOG_PATH, `${lines.slice(0, 2).join('\n')}\n`);
    expect(auditVerify(container)).toMatchObject({ ok: true, entries: 2 });
  });
});

describe('audit.checkpoint', () => {
  it('is absent-but-fine on a container that has none', () => {
    expect(auditVerifyCheckpoint(new MemoryContainer())).toMatchObject({
      present: false,
      valid: false,
    });
  });

  it('signs the current head and verifies against it', () => {
    const key = SigningKey.generate('EdDSA');
    const container = chained(['CREATED', 'SIGNED']);
    const head = auditCheckpoint(container, key);

    expect(container.has(AUDIT_CHECKPOINT_PATH)).toBe(true);
    const result = auditVerifyCheckpoint(container);
    expect(result).toMatchObject({ present: true, valid: true, head, kid: key.didKey() });
  });

  it('catches the truncation the chain alone cannot', () => {
    const key = SigningKey.generate('EdDSA');
    const container = chained(['CREATED', 'SIGNED', 'ENCRYPTED']);
    auditCheckpoint(container, key);

    const lines = container.readText(AUDIT_LOG_PATH).trimEnd().split('\n');
    container.writeText(AUDIT_LOG_PATH, `${lines.slice(0, 2).join('\n')}\n`);

    expect(auditVerify(container).ok).toBe(true); // the chain still looks fine
    const result = auditVerifyCheckpoint(container);
    expect(result.valid).toBe(false); // ...but the signed head is gone
    expect(result.error).toMatch(/not in the current chain/);
  });

  it('stays valid as later entries are appended', () => {
    // A checkpoint asserts "the log contained at least this"; it is not
    // invalidated by honest growth afterwards.
    const key = SigningKey.generate('EdDSA');
    const container = chained(['CREATED']);
    auditCheckpoint(container, key);
    auditAppend(container, 'SIGNED', 'tester', T(5));

    expect(auditVerifyCheckpoint(container).valid).toBe(true);
  });

  it('rejects a checkpoint whose signature was replaced', () => {
    const key = SigningKey.generate('EdDSA');
    const impostor = SigningKey.generate('EdDSA');
    const container = chained(['CREATED']);
    auditCheckpoint(container, key);

    const stored = JSON.parse(container.readText(AUDIT_CHECKPOINT_PATH)) as { head: string };
    const forged = chained(['SOMETHING ELSE']);
    auditCheckpoint(forged, impostor);
    const other = JSON.parse(forged.readText(AUDIT_CHECKPOINT_PATH)) as { signature: string };

    container.writeText(
      AUDIT_CHECKPOINT_PATH,
      JSON.stringify({ head: stored.head, signature: other.signature }, null, 2),
    );
    expect(auditVerifyCheckpoint(container).valid).toBe(false);
  });

  it('is excluded from the manifest, so appending never breaks integrity', async () => {
    const { buildManifest } = await import('./manifest.js');
    const container = chained(['CREATED']);
    auditCheckpoint(container, SigningKey.generate('EdDSA'));
    const files = Object.keys(buildManifest(container).files);
    expect(files).not.toContain(AUDIT_LOG_PATH);
    expect(files).not.toContain(AUDIT_CHECKPOINT_PATH);
  });
});
