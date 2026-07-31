// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// `audit.log` line format, ported from `src/serialize/audit_log.cpp`.
//
// One tab-separated line per entry: timestamp, action, actor, prev_hash. The
// line is hashed verbatim to chain the next entry, so its exact bytes matter as
// much as any other part of the format.

import type { AuditEntry } from '../model/types.js';
import { ParseError } from './yaml.js';

export const AUDIT_LOG_PATH = 'audit.log';
export const AUDIT_CHECKPOINT_PATH = 'audit.checkpoint';

/** The prev_hash of the first entry: 64 zeros. */
export const AUDIT_GENESIS = '0'.repeat(64);

export function auditEntryToLine(entry: AuditEntry): string {
  return `${entry.timestamp}\t${entry.action}\t${entry.actor}\t${entry.prev_hash}`;
}

/**
 * Parses the whole log. A line with the wrong number of fields is a parse
 * error, not a skipped line: silently dropping an unreadable entry would break
 * the chain and report the break as if the file were shorter than it is.
 */
export function parseAuditLog(text: string): AuditEntry[] {
  const entries: AuditEntry[] = [];
  for (const line of text.split('\n')) {
    if (line === '') continue;
    const fields = line.split('\t');
    if (fields.length !== 4) {
      throw new ParseError('malformed audit.log entry');
    }
    entries.push({
      timestamp: fields[0] as string,
      action: fields[1] as string,
      actor: fields[2] as string,
      prev_hash: fields[3] as string,
    });
  }
  return entries;
}
