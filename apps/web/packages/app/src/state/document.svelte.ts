// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The document store. It owns view state and edit buffers only — every format
// and integrity operation is delegated to `mcdf-ts`, the same layering law
// Studio follows with `studio_core`. No component parses or hashes anything.

import {
  CONTENT_PATH,
  EncPublicKey,
  MANIFEST_PATH,
  METADATA_PATH,
  MemoryContainer,
  SCHEMA_PATH,
  auditAppend,
  auditCheckpoint,
  auditVerify,
  auditVerifyCheckpoint,
  buildManifest,
  canonicalizeContent,
  createDocument,
  decryptContainer,
  emptyManifest,
  emptyMetadata,
  emptySchema,
  encryptContainer,
  encryptedMembers,
  isManifestExcluded,
  loadDocument,
  manifestToCanonicalJson,
  metadataToYaml,
  parseHeadings,
  readAuditLog,
  readEncryptionPolicy,
  renderHtml,
  schemaToYaml,
  setHeadingAnchor,
  sha256Hex,
  signContainer,
  signaturePath,
  utf8Encode,
  validate,
  verifyContainer,
  verifyManifest,
  type AuditEntry,
  type AuditVerification,
  type CheckpointResult,
  type DocumentSchema,
  type EncPrivateKey,
  type EncryptionPolicy,
  type Heading,
  type Manifest,
  type Metadata,
  type Profile,
  type SignatureCheck,
  type SigningKey,
  type ValidationReport,
} from 'mcdf-ts';

export type MemberState = 'unchanged' | 'modified' | 'unlisted' | 'excluded';

export interface MemberRow {
  path: string;
  size: number;
  state: MemberState;
  /** Digest of the member as it stands in the editor. */
  digest: string;
  /** Digest recorded in `manifest.json`, or '' when the member is unlisted. */
  recorded: string;
}

export interface OutlineRow {
  heading: Heading;
  /** Schema section bound to this heading, when there is one. */
  sectionTitle: string | null;
  required: boolean;
}

export interface UnboundSection {
  id: string;
  title: string;
  required: boolean;
}

export interface MemberChange {
  path: string;
  kind: 'added' | 'removed' | 'modified';
}

function bytesOf(container: MemoryContainer, path: string): number {
  return container.has(path) ? container.read(path).length : 0;
}

export class DocumentStore {
  /** The container as last opened or saved — the baseline edits diff against. */
  #base = new MemoryContainer();

  /** Bumped whenever `#base` is replaced, so deriveds that read it re-run. */
  #baseRevision = $state(0);

  /**
   * Set for a document that has never been written anywhere — currently only an
   * import. Cleared by `pack()`, which is the moment bytes leave the tab.
   */
  #unsaved = $state(false);

  fileName = $state('untitled.mcdf');
  content = $state('');
  metadata = $state<Metadata>(emptyMetadata());
  schema = $state<DocumentSchema>(emptySchema());
  hasSchema = $state(false);
  hasMetadata = $state(false);
  hasManifest = $state(false);
  storedManifest = $state<Manifest>(emptyManifest());
  profile = $state<Profile>('integrity');

  /** Set once a document has been opened or created (vs. the empty start state). */
  loaded = $state(false);

  constructor() {
    this.reset(createDocument('Untitled Document'), 'untitled.mcdf');
  }

  /**
   * Members that are ciphertext right now. Nothing may parse or overwrite
   * these: a text buffer written back over an encrypted member destroys it.
   */
  sealed = $state<Set<string>>(new Set());

  // -- loading ------------------------------------------------------------

  reset(container: MemoryContainer, fileName: string): void {
    const sealed = new Set(encryptedMembers(container));
    const doc = loadDocument(container);
    this.#base = container;
    this.#baseRevision++;
    this.sealed = sealed;
    this.fileName = fileName;
    // An encrypted content.md decodes to mojibake; showing that in the editor
    // would invite someone to "fix" it and overwrite the ciphertext.
    this.content = sealed.has(CONTENT_PATH) ? '' : doc.content;
    this.metadata = doc.metadata;
    this.schema = doc.schema;
    this.hasSchema = doc.hasSchema;
    this.hasMetadata = doc.hasMetadata;
    this.hasManifest = doc.hasManifest;
    this.storedManifest = doc.manifest;
    this.loaded = true;
    this.#unsaved = false;
  }

  openTar(archive: Uint8Array, fileName: string): void {
    this.reset(MemoryContainer.fromTar(archive), fileName);
  }

  /**
   * Adopts a container produced by an importer.
   *
   * Marked unsaved on purpose. An imported document exists only in this tab —
   * there is no file behind it — so treating it as clean would let the
   * beforeunload guard stay quiet while a whole converted book is one reload
   * away from being lost.
   */
  adoptImported(container: MemoryContainer, fileName: string): void {
    this.reset(container, fileName);
    this.#unsaved = true;
  }

  newDocument(title: string): void {
    this.reset(createDocument(title), 'untitled.mcdf');
  }

  // -- the working container ----------------------------------------------

  /**
   * The container as it would be written right now: the baseline members with
   * the live edit buffers applied. Everything downstream (hashes, validation,
   * export) reads this, so what you see is exactly what gets saved.
   */
  working = $derived.by((): MemoryContainer => {
    void this.#baseRevision;
    const c = this.#base.clone();
    const sealed = this.sealed;
    if (!sealed.has(CONTENT_PATH)) c.writeText(CONTENT_PATH, canonicalizeContent(this.content));
    if (this.hasMetadata && !sealed.has(METADATA_PATH)) {
      c.writeText(METADATA_PATH, metadataToYaml(this.metadata));
    }
    if (this.hasSchema && !sealed.has(SCHEMA_PATH)) {
      c.writeText(SCHEMA_PATH, schemaToYaml(this.schema));
    }
    return c;
  });

  headings = $derived<Heading[]>(parseHeadings(this.content));

  previewHtml = $derived<string>(renderHtml(this.content));

  /** Rebuilt from the working bytes — what the manifest *would* say if saved. */
  liveManifest = $derived<Manifest>(buildManifest(this.working));

  canonicalManifest = $derived<string>(manifestToCanonicalJson(this.liveManifest));

  /**
   * Identity of the packed document: the SHA-256 of the deterministic `.mcdf`
   * archive. Two containers with this hash are byte-identical.
   */
  containerHash = $derived<string>(sha256Hex(this.working.toTar()));

  dirty = $derived.by((): boolean => {
    void this.#baseRevision;
    if (this.#unsaved) return true;
    const working = this.working;
    const base = this.#base;
    const paths = new Set([...working.list(), ...base.list()]);
    for (const path of paths) {
      if (path === MANIFEST_PATH) continue; // rebuilt on save; never a reason to be dirty
      if (!working.has(path) || !base.has(path)) return true;
      if (sha256Hex(working.read(path)) !== sha256Hex(base.read(path))) return true;
    }
    return false;
  });

  // -- panels --------------------------------------------------------------

  members = $derived.by((): MemberRow[] => {
    const working = this.working;
    const recorded = this.hasManifest ? this.storedManifest.files : {};
    return working.list().map((path): MemberRow => {
      const digest = sha256Hex(working.read(path));
      if (isManifestExcluded(path)) {
        return { path, size: bytesOf(working, path), state: 'excluded', digest, recorded: '' };
      }
      const known = recorded[path];
      const state: MemberState =
        known === undefined ? 'unlisted' : known === digest ? 'unchanged' : 'modified';
      return {
        path,
        size: bytesOf(working, path),
        state,
        digest,
        recorded: known ?? '',
      };
    });
  });

  /** Members the manifest lists that are no longer present. */
  missingMembers = $derived.by((): string[] => {
    if (!this.hasManifest) return [];
    return verifyManifest(this.working, this.storedManifest).missing;
  });

  manifestUpToDate = $derived.by((): boolean => {
    if (!this.hasManifest) return false;
    return verifyManifest(this.working, this.storedManifest).ok;
  });

  outline = $derived.by((): OutlineRow[] => {
    const byId = new Map(this.schema.sections.map((s) => [s.id, s]));
    return this.headings.map((heading) => {
      const section = heading.id === '' ? undefined : byId.get(heading.id);
      return {
        heading,
        sectionTitle: section?.title ?? null,
        required: section?.required ?? false,
      };
    });
  });

  unboundSections = $derived.by((): UnboundSection[] => {
    const ids = new Set(this.headings.map((h) => h.id).filter((id) => id !== ''));
    return this.schema.sections
      .filter((s) => s.id !== '' && !ids.has(s.id))
      .map((s) => ({ id: s.id, title: s.title, required: s.required }));
  });

  report = $derived.by((): ValidationReport => {
    const container = this.working;
    const doc = loadDocument(container);
    return validate(container, doc, this.profile);
  });

  // -- diff against the baseline --------------------------------------------

  /** `content.md` as last opened or saved — the left-hand side of the diff. */
  baselineContent = $derived.by((): string => {
    void this.#baseRevision;
    return this.#base.has(CONTENT_PATH) && !this.sealed.has(CONTENT_PATH)
      ? this.#base.readText(CONTENT_PATH)
      : '';
  });

  /** `content.md` as it would be written right now. */
  workingContent = $derived.by((): string =>
    this.sealed.has(CONTENT_PATH) ? '' : canonicalizeContent(this.content),
  );

  /**
   * Members that differ from the baseline, by digest.
   *
   * Digests rather than text, so an attached image counts as a change — the
   * question this answers is "what would saving alter?", and a text diff can
   * only speak for text.
   */
  changedMembers = $derived.by((): MemberChange[] => {
    void this.#baseRevision;
    const working = this.working;
    const base = this.#base;
    const changes: MemberChange[] = [];

    for (const path of new Set([...working.list(), ...base.list()])) {
      // Rebuilt on save and excluded from itself; never a meaningful diff.
      if (path === MANIFEST_PATH) continue;
      const inWorking = working.has(path);
      const inBase = base.has(path);
      if (inWorking && !inBase) changes.push({ path, kind: 'added' });
      else if (!inWorking && inBase) changes.push({ path, kind: 'removed' });
      else if (sha256Hex(working.read(path)) !== sha256Hex(base.read(path))) {
        changes.push({ path, kind: 'modified' });
      }
    }
    return changes.sort((a, b) => a.path.localeCompare(b.path));
  });

  // -- trust ---------------------------------------------------------------

  /**
   * Signature verdicts against the manifest **as it would be saved**, not the
   * one currently in the file.
   *
   * That is the question the panel exists to answer. Checking against the
   * stored manifest would report VALID while the document changed under it, and
   * the whole point of the demo is that editing breaks trust the instant it
   * happens — visibly and, via the announcer, audibly.
   */
  signatures = $derived.by((): SignatureCheck[] => {
    const container = this.working;
    if (!container.has(MANIFEST_PATH)) return [];
    return verifyContainer(container, utf8Encode(this.canonicalManifest));
  });

  signedAndValid = $derived.by(
    (): boolean => this.signatures.length > 0 && this.signatures.every((s) => s.valid),
  );

  /**
   * Signs the document and adopts the result.
   *
   * The manifest is rebuilt first, because a signature over a stale manifest is
   * invalid the moment it is written — the reference CLI refuses outright
   * ("manifest does not match content"); here the fix is free, so it is applied
   * rather than reported.
   */
  sign(key: SigningKey, name: string): SignatureCheck {
    const container = this.working.clone();
    const manifest = buildManifest(container, this.storedManifest.hash_algorithm);
    container.writeText(MANIFEST_PATH, `${manifestToCanonicalJson(manifest)}\n`);

    const path = signaturePath(name);
    container.writeText(path, signContainer(container, key));

    this.#base = container;
    this.#baseRevision++;
    this.storedManifest = manifest;
    this.hasManifest = true;

    const check = verifyContainer(container).find((c) => c.file === path);
    if (check === undefined) throw new Error(`signature ${path} did not verify after signing`);
    return check;
  }

  removeSignature(path: string): void {
    const container = this.working.clone();
    container.remove(path);
    this.#base = container;
    this.#baseRevision++;
  }

  // -- confidentiality ------------------------------------------------------

  policy = $derived.by((): EncryptionPolicy | null => {
    void this.#baseRevision;
    return readEncryptionPolicy(this.#base);
  });

  encrypted = $derived<boolean>(this.policy !== null);

  /** Members that may be encrypted: content and assets, never the structure. */
  encryptableMembers = $derived.by((): string[] =>
    this.working
      .list()
      .filter(
        (p) =>
          !isManifestExcluded(p) &&
          p !== METADATA_PATH &&
          p !== SCHEMA_PATH &&
          !p.startsWith('encryption/'),
      ),
  );

  /**
   * Encrypts members for the given recipients.
   *
   * `metadata.yaml` and `schema.yaml` are deliberately not offered: the panels
   * that read them would have nothing to show, and validation of an encrypted
   * schema reports a section as *missing* when it is merely unreadable. The CLI
   * still allows it with `--file`, matching the reference implementation.
   */
  async encrypt(files: string[], recipientDids: string[]): Promise<void> {
    const container = this.working.clone();
    const recipients = recipientDids.map((did) => EncPublicKey.fromDidKey(did));
    await encryptContainer(container, files, recipients);
    this.reset(container, this.fileName);
  }

  async decrypt(key: EncPrivateKey): Promise<string[]> {
    const container = this.working.clone();
    const files = await decryptContainer(container, key);
    this.reset(container, this.fileName);
    return files;
  }

  // -- audit ----------------------------------------------------------------

  auditEntries = $derived.by((): AuditEntry[] => {
    void this.#baseRevision;
    try {
      return readAuditLog(this.#base);
    } catch {
      // A malformed log is reported by `auditState` below; the timeline just
      // has nothing it can honestly render.
      return [];
    }
  });

  auditState = $derived.by((): AuditVerification => {
    void this.#baseRevision;
    return auditVerify(this.#base);
  });

  checkpoint = $derived.by((): CheckpointResult => {
    void this.#baseRevision;
    return auditVerifyCheckpoint(this.#base);
  });

  appendAudit(action: string, actor: string): AuditEntry {
    const container = this.working.clone();
    const entry = auditAppend(container, action, actor, new Date().toISOString().slice(0, 19) + 'Z');
    // audit.log is excluded from the manifest, so this cannot break integrity —
    // which is exactly why the log can grow after signing.
    this.#base = container;
    this.#baseRevision++;
    return entry;
  }

  writeCheckpoint(key: SigningKey): string {
    const container = this.working.clone();
    const head = auditCheckpoint(container, key);
    this.#base = container;
    this.#baseRevision++;
    return head;
  }


  // -- saving --------------------------------------------------------------

  /**
   * Packs the current state into `.mcdf` bytes, rebuilding the manifest when the
   * document has one, and adopts the result as the new baseline.
   */
  pack(): Uint8Array {
    const container = this.working.clone();
    if (this.hasManifest) {
      const manifest = buildManifest(container, this.storedManifest.hash_algorithm);
      container.writeText(MANIFEST_PATH, `${manifestToCanonicalJson(manifest)}\n`);
      this.storedManifest = manifest;
    }
    const bytes = container.toTar();
    this.#base = container;
    this.#baseRevision++;
    this.#unsaved = false;
    return bytes;
  }

  /** Adds Integrity: builds `manifest.json` for a document that has none. */
  addManifest(): void {
    const container = this.working.clone();
    const manifest = buildManifest(container);
    container.writeText(MANIFEST_PATH, `${manifestToCanonicalJson(manifest)}\n`);
    this.#base = container;
    this.#baseRevision++;
    this.storedManifest = manifest;
    this.hasManifest = true;
  }

  addSchema(): void {
    this.schema = emptySchema();
    this.schema.document_type = 'document';
    this.hasSchema = true;
  }

  /**
   * Binds a schema section to a heading by writing its `{#id}` anchor onto that
   * heading — the repair for E_SCHEMA_UNBOUND and E_REQUIRED_SECTION_MISSING.
   */
  bindSection(sectionId: string, headingLine: number): void {
    this.content = setHeadingAnchor(this.content, headingLine, sectionId);
  }

  addSection(): void {
    if (!this.hasSchema) this.addSchema();
    this.schema = {
      ...this.schema,
      sections: [...this.schema.sections, { id: '', title: '', required: false }],
    };
  }

  removeSection(index: number): void {
    this.schema = {
      ...this.schema,
      sections: this.schema.sections.filter((_, i) => i !== index),
    };
  }

  moveSection(index: number, delta: number): void {
    const target = index + delta;
    const sections = [...this.schema.sections];
    if (target < 0 || target >= sections.length) return;
    const [moved] = sections.splice(index, 1);
    if (moved === undefined) return;
    sections.splice(target, 0, moved);
    this.schema = { ...this.schema, sections };
  }

  /**
   * Replaces the schema's sections with one per heading that carries an anchor —
   * the other way to resolve a mismatch, when the content is right and the
   * schema is the stale part.
   */
  adoptSchemaFromContent(): void {
    if (!this.hasSchema) this.addSchema();
    const sections = this.headings
      .filter((h) => h.id !== '')
      .map((h) => ({ id: h.id, title: h.text, required: false }));
    this.schema = { ...this.schema, sections };
  }

  addMetadata(): void {
    this.metadata = emptyMetadata();
    this.hasMetadata = true;
  }

  /** Attaches an arbitrary member (an image, a diagram) to the container. */
  attach(path: string, data: Uint8Array): void {
    const container = this.working.clone();
    container.write(path, data);
    this.#base = container;
    this.#baseRevision++;
  }

  removeMember(path: string): void {
    const container = this.working.clone();
    container.remove(path);
    this.#base = container;
    this.#baseRevision++;
  }

  /** Bytes of a member, for download or preview. */
  readMember(path: string): Uint8Array {
    return this.working.read(path);
  }

  /** Bytes of a member, or null when it is not in the container. */
  tryReadMember(path: string): Uint8Array | null {
    return this.working.has(path) ? this.working.read(path) : null;
  }

  /** Member path -> current digest; the change key for cached asset URLs. */
  memberDigests = $derived.by(
    (): Map<string, string> => new Map(this.members.map((m) => [m.path, m.digest])),
  );

  /** Size of the packed document, for display. */
  packedSize = $derived<number>(this.working.toTar().length);

  /** Convenience for tests and the tamper demo. */
  tamperContent(text: string): void {
    this.content = text;
  }

  static utf8(text: string): Uint8Array {
    return utf8Encode(text);
  }
}
