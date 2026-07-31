// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// `encryption/policy.yaml`, ported from `src/serialize/policy_yaml.cpp`.
//
// Emission is hand-rolled for the same reason metadata's is: the policy file is
// hashed by the manifest, and the manifest is what signatures cover, so its
// byte layout is part of the format rather than a formatting preference.

import yaml from 'js-yaml';

import {
  emptyEncryptionPolicy,
  type EncryptionPolicy,
  type Recipient,
} from '../model/types.js';
import { ParseError } from './yaml.js';

export const ENCRYPTION_POLICY_PATH = 'encryption/policy.yaml';

function asRecord(value: unknown): Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : {};
}

function str(node: Record<string, unknown>, key: string): string {
  const v = node[key];
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  return '';
}

export function parseEncryptionPolicyYaml(text: string): EncryptionPolicy {
  let loaded: unknown;
  try {
    loaded = yaml.load(text, { schema: yaml.JSON_SCHEMA });
  } catch (e) {
    throw new ParseError(`policy.yaml: ${(e as Error).message}`);
  }

  const n = asRecord(loaded);
  const policy = emptyEncryptionPolicy();
  if (n['method'] !== undefined) policy.method = str(n, 'method');
  if (n['key_management'] !== undefined) policy.key_management = str(n, 'key_management');

  if (Array.isArray(n['encrypted_files'])) {
    for (const f of n['encrypted_files']) {
      if (typeof f === 'string') policy.encrypted_files.push(f);
    }
  }
  if (n['structure'] !== undefined && n['structure'] !== null) {
    policy.structure.present = true;
    const bound = asRecord(n['structure'])['bound_sections'];
    if (Array.isArray(bound)) {
      for (const id of bound) {
        if (typeof id === 'string') policy.structure.bound_sections.push(id);
      }
    }
  }
  if (Array.isArray(n['recipients'])) {
    for (const raw of n['recipients']) {
      const r = asRecord(raw);
      const recipient: Recipient = {
        id: str(r, 'id'),
        enc: str(r, 'enc'),
        wrapped_key: str(r, 'wrapped_key'),
      };
      policy.recipients.push(recipient);
    }
  }
  return policy;
}

/**
 * Emits the policy exactly as `policy_yaml.cpp` does.
 *
 * Every value here is machine-generated and known-safe as a plain scalar — a
 * lowercase algorithm name, a member path, a `did:key`, base64url — so no
 * quoting logic is needed, and adding some would be the thing that broke parity.
 */
export function encryptionPolicyToYaml(policy: EncryptionPolicy): string {
  let out = '';
  out += `method: ${policy.method}\n`;
  out += `key_management: ${policy.key_management}\n`;
  out += 'encrypted_files:\n';
  for (const f of policy.encrypted_files) out += `  - ${f}\n`;
  // Between the file list and the recipients: it describes what was sealed, not
  // who can open it. An empty list is emitted as `[]` rather than omitted — the
  // claim "no section bound" is a real claim and must not read as no claim.
  if (policy.structure.present) {
    out += 'structure:\n';
    if (policy.structure.bound_sections.length === 0) {
      out += '  bound_sections: []\n';
    } else {
      out += '  bound_sections:\n';
      for (const id of policy.structure.bound_sections) out += `    - ${id}\n`;
    }
  }
  out += 'recipients:\n';
  for (const r of policy.recipients) {
    out += `  - id: ${r.id}\n`;
    out += `    enc: ${r.enc}\n`;
    out += `    wrapped_key: ${r.wrapped_key}\n`;
  }
  return out;
}
