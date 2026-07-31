// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

/** The MCDF version this implementation writes into manifests. */
export const MCDF_VERSION = '1.0';

/** The only hash algorithm the Integrity profile allows. */
export const DEFAULT_HASH_ALGORITHM = 'sha256';

export interface Author {
  name: string;
  id: string;
}

export interface Metadata {
  title: string;
  version: string;
  authors: Author[];
  created_at: string;
  classification: string;
  language: string;
  /** Provenance stamp written by importers; absent on hand-authored documents. */
  generated_by?: string;
}

export interface SchemaSection {
  id: string;
  title: string;
  required: boolean;
}

export interface DocumentSchema {
  document_type: string;
  sections: SchemaSection[];
}

export interface Manifest {
  mcdf_version: string;
  hash_algorithm: string;
  /** Member path -> lowercase hex digest. */
  files: Record<string, string>;
}

export interface Heading {
  level: number;
  text: string;
  /** The `{#id}` anchor, or '' when the heading carries none. */
  id: string;
  /** 1-based line in `content.md` where the heading starts. */
  line: number;
  /**
   * False when the heading sits inside a block quote, list item or table cell.
   *
   * Only a top-level heading binds a `schema.yaml` section (spec §4.2); every
   * heading, nested or not, still renders and still carries its anchor as an
   * `id` (spec §10.4). So this filters binding and nothing else — the list
   * itself stays complete.
   */
  topLevel: boolean;
}

/** An in-memory view of a document loaded from a container. */
export interface McdfDocument {
  metadata: Metadata;
  schema: DocumentSchema;
  manifest: Manifest;
  /** Raw `content.md` text; empty when the member is sealed. */
  content: string;
  headings: Heading[];

  /**
   * Members whose stored bytes are ciphertext (spec §5.2). A sealed member is
   * present but not parsed: `hasContent` stays true for a sealed `content.md`
   * (the member is there) while `headings` stays empty (nothing readable), and
   * a sealed `schema.yaml` leaves `hasSchema` false — a schema nobody can read
   * makes no structural claims.
   */
  sealed: string[];

  hasMetadata: boolean;
  hasSchema: boolean;
  hasManifest: boolean;
  hasContent: boolean;
}

/** A per-recipient wrapped content-encryption key in `encryption/policy.yaml`. */
export interface Recipient {
  /** Recipient's X25519 `did:key`. */
  id: string;
  /** base64url HPKE encapsulated key. */
  enc: string;
  /** base64url HPKE-sealed CEK. */
  wrapped_key: string;
}

/**
 * The structure attestation (spec §5.2.1): which `schema.yaml` sections bound
 * to a heading at the moment `content.md` was sealed.
 *
 * A validator without the key cannot see the headings, so this is what it
 * evaluates §4.2 against. It is a statement by the encryptor rather than a
 * proof — what makes it worth something is that the manifest covers it (so a
 * signature attributes it) and that `decryptContainer` re-checks it against the
 * recovered headings, so a false attestation does not survive the first
 * recipient who opens the document.
 */
export interface StructureAttestation {
  /**
   * Absent entirely for containers with no readable schema. "No section bound"
   * and "no claim made" are different states and must stay distinguishable.
   */
  present: boolean;
  bound_sections: string[];
}

/** `encryption/policy.yaml`: what is encrypted, and how the CEK is wrapped. */
export interface EncryptionPolicy {
  method: string;
  key_management: string;
  encrypted_files: string[];
  structure: StructureAttestation;
  recipients: Recipient[];
}

export function emptyEncryptionPolicy(): EncryptionPolicy {
  return {
    method: DEFAULT_ENCRYPTION_METHOD,
    key_management: DEFAULT_KEY_MANAGEMENT,
    encrypted_files: [],
    structure: { present: false, bound_sections: [] },
    recipients: [],
  };
}

/** The only content-encryption algorithm the Encrypted profile allows. */
export const DEFAULT_ENCRYPTION_METHOD = 'aes-256-gcm';

/** The only key-wrapping scheme the Encrypted profile allows. */
export const DEFAULT_KEY_MANAGEMENT = 'hpke';

/**
 * One line of `audit.log`. `prev_hash` chains each entry to the one before it,
 * so truncation, reordering and edits are all detectable.
 */
export interface AuditEntry {
  /** RFC 3339. */
  timestamp: string;
  /** CREATED, SIGNED, ENCRYPTED, … */
  action: string;
  /** A name or a `did:key`. */
  actor: string;
  /** Hex SHA-256 of the previous entry's line; the genesis value is 64 zeros. */
  prev_hash: string;
}

export function emptyMetadata(): Metadata {
  return {
    title: '',
    version: '',
    authors: [],
    created_at: '',
    classification: '',
    language: '',
  };
}

export function emptySchema(): DocumentSchema {
  return { document_type: '', sections: [] };
}

export function emptyManifest(): Manifest {
  return {
    mcdf_version: MCDF_VERSION,
    hash_algorithm: DEFAULT_HASH_ALGORITHM,
    // Null prototype: member paths are attacker-controlled, and on a plain
    // object `'toString' in files` would be true (falsely "listed") while
    // `files['__proto__'] = digest` would mutate the prototype instead of
    // recording a member.
    files: Object.create(null) as Record<string, string>,
  };
}
