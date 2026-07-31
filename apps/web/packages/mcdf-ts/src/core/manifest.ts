// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

import type { Container } from '../container/container.js';
import { hashHex } from '../crypto/hash.js';
import {
  DEFAULT_HASH_ALGORITHM,
  MCDF_VERSION,
  emptyManifest,
  type Manifest,
} from '../model/types.js';
import { canonicalizeValue, type JsonValue } from '../serialize/jcs.js';
import { ParseError } from '../serialize/yaml.js';

/**
 * Members the manifest MUST NOT list (conformance/errors.md, "Manifest
 * membership"): the manifest itself, and the members that legitimately change
 * after signing.
 */
export function isManifestExcluded(path: string): boolean {
  return (
    path === 'manifest.json' ||
    path === 'audit.log' ||
    path === 'audit.checkpoint' ||
    path.startsWith('signatures/')
  );
}

export function buildManifest(
  container: Container,
  algorithm: string = DEFAULT_HASH_ALGORITHM,
): Manifest {
  const m = emptyManifest();
  m.mcdf_version = MCDF_VERSION;
  m.hash_algorithm = algorithm;
  for (const path of container.list()) {
    if (isManifestExcluded(path)) continue;
    m.files[path] = hashHex(algorithm, container.read(path));
  }
  return m;
}

/** Serializes a manifest in RFC 8785 canonical form — the signing payload. */
export function manifestToCanonicalJson(manifest: Manifest): string {
  const value: JsonValue = {
    mcdf_version: manifest.mcdf_version,
    hash_algorithm: manifest.hash_algorithm,
    files: { ...manifest.files },
  };
  return canonicalizeValue(value);
}

export function parseManifestJson(text: string): Manifest {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch (e) {
    throw new ParseError(`manifest.json: ${(e as Error).message}`);
  }
  const j = (typeof parsed === 'object' && parsed !== null ? parsed : {}) as Record<
    string,
    unknown
  >;
  const m = emptyManifest();
  if (typeof j['mcdf_version'] === 'string') m.mcdf_version = j['mcdf_version'];
  if (typeof j['hash_algorithm'] === 'string') m.hash_algorithm = j['hash_algorithm'];
  const files = j['files'];
  if (typeof files === 'object' && files !== null && !Array.isArray(files)) {
    for (const [path, hash] of Object.entries(files as Record<string, unknown>)) {
      if (typeof hash === 'string') m.files[path] = hash;
    }
  }
  return m;
}

export interface ManifestVerification {
  ok: boolean;
  /** Listed in the manifest but absent from the container. */
  missing: string[];
  /** Present and listed, but the recomputed digest differs. */
  mismatched: string[];
  /** Present and not excluded, but unlisted. */
  extra: string[];
}

export function verifyManifest(
  container: Container,
  manifest: Manifest,
): ManifestVerification {
  const v: ManifestVerification = { ok: false, missing: [], mismatched: [], extra: [] };

  for (const [path, expected] of Object.entries(manifest.files)) {
    if (!container.has(path)) {
      v.missing.push(path);
      continue;
    }
    if (hashHex(manifest.hash_algorithm, container.read(path)) !== expected) {
      v.mismatched.push(path);
    }
  }

  for (const path of container.list()) {
    if (isManifestExcluded(path)) continue;
    if (!(path in manifest.files)) v.extra.push(path);
  }

  v.ok = v.missing.length === 0 && v.mismatched.length === 0 && v.extra.length === 0;
  return v;
}
