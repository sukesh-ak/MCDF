// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The audit chain, ported from `src/core/audit_ops.cpp`.
//
// Each entry records the SHA-256 of the previous entry's line, so the log
// detects edits, reordering and truncation-with-replacement. It cannot detect
// plain truncation of the tail on its own — that is what `audit.checkpoint`
// is for: a signature over the head the signer saw.

import type { Container, WritableContainer } from '../container/container.js';
import { sha256Hex } from '../crypto/hash.js';
import { jwsSignDetached, jwsVerifyDetached } from '../crypto/jws.js';
import type { SigningKey } from '../crypto/keys.js';
import type { AuditEntry } from '../model/types.js';
import {
  AUDIT_CHECKPOINT_PATH,
  AUDIT_GENESIS,
  AUDIT_LOG_PATH,
  auditEntryToLine,
  parseAuditLog,
} from '../serialize/audit-log.js';
import { utf8Encode } from '../util/bytes.js';

export function entryHash(entry: AuditEntry): string {
  return sha256Hex(utf8Encode(auditEntryToLine(entry)));
}

export function readAuditLog(container: Container): AuditEntry[] {
  if (!container.has(AUDIT_LOG_PATH)) return [];
  return parseAuditLog(container.readText(AUDIT_LOG_PATH));
}

/** The hash the next entry must chain to. */
export function auditHead(container: Container): string {
  const entries = readAuditLog(container);
  const last = entries[entries.length - 1];
  return last === undefined ? AUDIT_GENESIS : entryHash(last);
}

/**
 * Appends an entry chained to the current head.
 *
 * `timestamp` is a parameter rather than being read from the clock here: the
 * core has no business deciding what time it is, and tests need entries whose
 * hashes are reproducible.
 */
export function auditAppend(
  container: WritableContainer,
  action: string,
  actor: string,
  timestamp: string,
): AuditEntry {
  const entry: AuditEntry = {
    timestamp,
    action,
    actor,
    prev_hash: auditHead(container),
  };
  container.append(AUDIT_LOG_PATH, `${auditEntryToLine(entry)}\n`);
  return entry;
}

export interface AuditVerification {
  ok: boolean;
  entries: number;
  /** Set when the chain is broken. */
  error: string;
  /** Index of the first entry whose prev_hash is wrong, or -1. */
  brokenAt: number;
}

/** Walks the chain from genesis. */
export function auditVerify(container: Container): AuditVerification {
  let entries: AuditEntry[];
  try {
    entries = readAuditLog(container);
  } catch (e) {
    return { ok: false, entries: 0, error: (e as Error).message, brokenAt: -1 };
  }

  let expected = AUDIT_GENESIS;
  for (let i = 0; i < entries.length; i++) {
    const entry = entries[i] as AuditEntry;
    if (entry.prev_hash !== expected) {
      return {
        ok: false,
        entries: entries.length,
        error: `hash chain broken at entry ${i}`,
        brokenAt: i,
      };
    }
    expected = entryHash(entry);
  }
  return { ok: true, entries: entries.length, error: '', brokenAt: -1 };
}

/** Signs the current head into `audit.checkpoint`. */
export function auditCheckpoint(
  container: WritableContainer,
  key: SigningKey,
  kid?: string,
): string {
  const head = auditHead(container);
  const signature = jwsSignDetached(key, utf8Encode(head), kid ?? key.didKey());
  // Two-space indent, matching nlohmann's `dump(2)` in the reference — the
  // checkpoint is excluded from the manifest, but a byte-identical file is
  // still the easiest thing to diff across implementations.
  container.writeText(
    AUDIT_CHECKPOINT_PATH,
    `${JSON.stringify({ head, signature }, null, 2)}`,
  );
  return head;
}

export interface CheckpointResult {
  present: boolean;
  valid: boolean;
  /** Signer's `did:key`. */
  kid: string;
  /** The head the checkpoint committed to. */
  head: string;
  error: string;
}

/**
 * Verifies `audit.checkpoint`: the signature must be good *and* its head must
 * still be reachable along an intact chain from genesis.
 *
 * The second half is the part that catches tail truncation: a valid signature
 * over a head that is no longer in the log means entries were removed.
 */
export function auditVerifyCheckpoint(container: Container): CheckpointResult {
  const result: CheckpointResult = { present: false, valid: false, kid: '', head: '', error: '' };
  if (!container.has(AUDIT_CHECKPOINT_PATH)) return result;
  result.present = true;

  let head = '';
  let signature = '';
  try {
    const parsed = JSON.parse(container.readText(AUDIT_CHECKPOINT_PATH)) as unknown;
    const j = (typeof parsed === 'object' && parsed !== null ? parsed : {}) as Record<string, unknown>;
    head = typeof j['head'] === 'string' ? j['head'] : '';
    signature = typeof j['signature'] === 'string' ? j['signature'] : '';
  } catch (e) {
    result.error = `audit.checkpoint: ${(e as Error).message}`;
    return result;
  }
  result.head = head;

  const verdict = jwsVerifyDetached(signature, utf8Encode(head));
  result.kid = verdict.kid;
  if (verdict.error !== '') result.error = verdict.error;

  let headIntact = head === AUDIT_GENESIS;
  let expected = AUDIT_GENESIS;
  let entries: AuditEntry[];
  try {
    entries = readAuditLog(container);
  } catch (e) {
    result.error = (e as Error).message;
    return result;
  }
  for (const entry of entries) {
    if (entry.prev_hash !== expected) break; // the chain broke before the head
    const hash = entryHash(entry);
    if (hash === head) {
      headIntact = true;
      break;
    }
    expected = hash;
  }

  if (verdict.valid && !headIntact) {
    result.error = 'the checkpointed head is not in the current chain (entries were removed)';
  }
  result.valid = verdict.valid && headIntact;
  return result;
}
