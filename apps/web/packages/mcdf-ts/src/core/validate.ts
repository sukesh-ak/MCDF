// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Profile validation, ported from `src/core/validate.cpp`. The error codes are
// normative (conformance/errors.md) — messages are free-form, codes are not.

import { ContainerError, checkMemberPath, type Container } from '../container/container.js';
import { EncPublicKey } from '../crypto/enc-keys.js';
import { isAllowedHashAlgorithm } from '../crypto/hash.js';
import type { McdfDocument } from '../model/types.js';
import { ENCRYPTION_POLICY_PATH, parseEncryptionPolicyYaml } from '../serialize/policy.js';
import { verifyManifest } from './manifest.js';
import { RENDER_FORMATS, renderCanonical } from './render.js';
import { encryptedMembers, readEncryptionPolicy } from './sealed.js';
import { verifyContainer } from './sign.js';

export const PROFILES = ['core', 'integrity', 'signed', 'encrypted', 'render'] as const;

export type Profile = (typeof PROFILES)[number];

const PROFILE_RANK: Record<Profile, number> = {
  core: 0,
  integrity: 1,
  signed: 2,
  encrypted: 3,
  render: 4,
};

export function isProfile(name: string): name is Profile {
  return (PROFILES as readonly string[]).includes(name);
}

export interface ValidationIssue {
  code: string;
  message: string;
}

export interface ValidationReport {
  profile: Profile;
  ok: boolean;
  issues: ValidationIssue[];
}

function atLeast(profile: Profile, floor: Profile): boolean {
  return PROFILE_RANK[profile] >= PROFILE_RANK[floor];
}

function checkCore(container: Container, doc: McdfDocument, issues: ValidationIssue[]): void {
  if (!doc.hasContent) {
    issues.push({ code: 'E_MISSING_CONTENT', message: 'content.md is missing' });
  }

  for (const path of container.list()) {
    try {
      checkMemberPath(path);
    } catch (e) {
      const err = e as ContainerError;
      issues.push({ code: err.code ?? 'E_ASSET_PATH_ESCAPE', message: err.message });
    }
  }

  if (!doc.hasSchema) return; // no schema, no structural claims to check

  // The set of section ids that bind. Normally that comes from the headings; a
  // sealed content.md has none a validator can read, so spec §5.2.1 puts the
  // same information in the policy and the rule below does not change.
  const contentSealed = doc.sealed.includes('content.md');
  let bound: Set<string>;

  if (contentSealed) {
    const policy = readEncryptionPolicy(container);
    if (policy === null || !policy.structure.present) {
      // Without the attestation the binding is simply not knowable. Saying so
      // once is right; reporting every required section as "missing" would
      // blame the document for the encryptor's omission.
      issues.push({
        code: 'E_POLICY_INVALID',
        message:
          'content.md is sealed but encryption/policy.yaml carries no structure ' +
          'attestation, so schema binding cannot be evaluated (spec §5.2.1)',
      });
      return;
    }
    bound = new Set(policy.structure.bound_sections);
  } else {
    bound = new Set(doc.headings.map((h) => h.id).filter((id) => id !== ''));
  }

  const where = contentSealed ? 'in the sealed content' : 'in content';
  for (const s of doc.schema.sections) {
    if (s.id === '' || bound.has(s.id)) continue;
    issues.push(
      s.required
        ? {
            code: 'E_REQUIRED_SECTION_MISSING',
            message: `required section has no matching heading ${where}: ${s.id}`,
          }
        : {
            code: 'E_SCHEMA_UNBOUND',
            message: `schema section has no matching heading ${where}: ${s.id}`,
          },
    );
  }
}

function checkIntegrity(
  container: Container,
  doc: McdfDocument,
  issues: ValidationIssue[],
): void {
  if (!doc.hasManifest) {
    issues.push({ code: 'E_MISSING_MANIFEST', message: 'manifest.json is missing' });
    return;
  }
  if (!isAllowedHashAlgorithm(doc.manifest.hash_algorithm)) {
    issues.push({
      code: 'E_ALGO_NOT_ALLOWED',
      message: `hash algorithm not allowed: ${doc.manifest.hash_algorithm}`,
    });
    return;
  }

  const v = verifyManifest(container, doc.manifest);
  for (const p of v.missing) {
    issues.push({
      code: 'E_MANIFEST_MISSING_FILE',
      message: `file listed in manifest is missing: ${p}`,
    });
  }
  for (const p of v.mismatched) {
    issues.push({ code: 'E_MANIFEST_HASH_MISMATCH', message: `content hash mismatch: ${p}` });
  }
  for (const p of v.extra) {
    issues.push({
      code: 'E_MANIFEST_EXTRA_FILE',
      message: `file present but not listed in manifest: ${p}`,
    });
  }
}

/**
 * Every `signatures/*.sig` must verify over the canonical manifest.
 *
 * Each failure carries its own normative code out of the JWS verifier, so a
 * signature that is merely wrong stays distinguishable from one that could not
 * be evaluated — an unresolvable `kid` and a forged signature are different
 * problems with different fixes.
 */
function checkSigned(container: Container, issues: ValidationIssue[]): void {
  let checks;
  try {
    checks = verifyContainer(container);
  } catch (e) {
    // A missing manifest is already reported by the Integrity checks, which
    // always run first at this profile; reporting it twice would be noise.
    const code = (e as { code?: string }).code;
    if (code !== 'E_MISSING_MANIFEST') {
      issues.push({ code: code ?? 'E_IO', message: (e as Error).message });
    }
    return;
  }

  if (checks.length === 0) {
    issues.push({ code: 'E_SIG_MISSING', message: 'no signatures present (signatures/*.sig)' });
    return;
  }

  for (const check of checks) {
    if (check.valid) continue;
    issues.push({
      code: check.code ?? 'E_SIG_INVALID',
      message: `${check.file}: ${
        check.error === ''
          ? 'signature does not verify over the canonical manifest'
          : check.error
      }`,
    });
  }
}

/**
 * Static soundness of the encryption layer. No key is available here, so the
 * decrypt-time codes (E_NOT_A_RECIPIENT, E_DECRYPT_FAILED) stay out of scope;
 * a container with no policy has nothing to assert and passes.
 */
function checkEncrypted(container: Container, issues: ValidationIssue[]): void {
  if (!container.has(ENCRYPTION_POLICY_PATH)) return;

  let policy;
  try {
    policy = parseEncryptionPolicyYaml(container.readText(ENCRYPTION_POLICY_PATH));
  } catch (e) {
    issues.push({ code: 'E_POLICY_INVALID', message: (e as Error).message });
    return;
  }

  if (policy.method !== 'aes-256-gcm') {
    issues.push({
      code: 'E_ALGO_NOT_ALLOWED',
      message: `encryption method not allowed: ${policy.method}`,
    });
  }
  if (policy.key_management !== 'hpke') {
    issues.push({
      code: 'E_ALGO_NOT_ALLOWED',
      message: `key management not allowed: ${policy.key_management}`,
    });
  }
  if (policy.encrypted_files.length === 0) {
    issues.push({ code: 'E_POLICY_INVALID', message: 'policy lists no encrypted files' });
  }
  for (const file of policy.encrypted_files) {
    if (!container.has(file)) {
      issues.push({
        code: 'E_POLICY_INVALID',
        message: `encrypted file listed in policy is missing: ${file}`,
      });
    }
  }
  if (policy.recipients.length === 0) {
    issues.push({ code: 'E_POLICY_INVALID', message: 'policy has no recipients' });
  }

  // The attestation's *presence* rules (spec §5.2.1). The "required but absent"
  // case is reported by checkCore, which runs at every profile; what is left
  // here is the reverse — an attestation nobody can corroborate, which either
  // leaks the schema or claims something about content that is not sealed.
  if (policy.structure.present) {
    const sealed = encryptedMembers(container);
    if (!sealed.includes('content.md')) {
      issues.push({
        code: 'E_POLICY_INVALID',
        message: 'policy carries a structure attestation but content.md is not encrypted',
      });
    } else if (sealed.includes('schema.yaml') || !container.has('schema.yaml')) {
      issues.push({
        code: 'E_POLICY_INVALID',
        message:
          'policy carries a structure attestation with no readable schema.yaml to ' +
          'corroborate it (spec §5.2.1)',
      });
    }
  }
  for (const recipient of policy.recipients) {
    if (recipient.id === '' || recipient.enc === '' || recipient.wrapped_key === '') {
      issues.push({ code: 'E_POLICY_INVALID', message: 'recipient entry is incomplete' });
      continue;
    }
    try {
      EncPublicKey.fromDidKey(recipient.id);
    } catch {
      issues.push({
        code: 'E_KID_UNRESOLVABLE',
        message: `recipient id is not a resolvable X25519 did:key: ${recipient.id}`,
      });
    }
  }
}

/**
 * Both canonical renders must be producible — except over sealed content, which
 * cannot be rendered at all, so the profile is not applicable rather than failed
 * (spec §6).
 *
 * This used to report `E_UNIMPLEMENTED`: the render had to be byte-identical to
 * the reference and there was nothing published to prove that against. §10.4 and
 * the `canonical/render-*` vectors closed both halves, so the profile is claimed.
 */
function checkRender(container: Container, doc: McdfDocument, issues: ValidationIssue[]): void {
  if (doc.sealed.includes('content.md')) return;
  for (const format of RENDER_FORMATS) {
    try {
      renderCanonical(container, format);
    } catch (e) {
      issues.push({ code: 'E_IO', message: `render ${format} failed: ${(e as Error).message}` });
    }
  }
}

export function validate(
  container: Container,
  doc: McdfDocument,
  profile: Profile,
): ValidationReport {
  const issues: ValidationIssue[] = [];

  checkCore(container, doc, issues);
  if (atLeast(profile, 'integrity')) checkIntegrity(container, doc, issues);
  if (atLeast(profile, 'signed')) checkSigned(container, issues);
  if (atLeast(profile, 'encrypted')) checkEncrypted(container, issues);
  if (atLeast(profile, 'render')) checkRender(container, doc, issues);

  return { profile, ok: issues.length === 0, issues };
}
