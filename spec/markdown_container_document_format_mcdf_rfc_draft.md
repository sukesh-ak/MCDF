<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 The MCDF Project. Licensed under the Community Specification License 1.0; see the LICENSE file in this directory. -->

# RFC: Markdown Container Document Format (MCDF)

## Status
Draft

## Requirements Language
The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be interpreted as described in RFC 2119 and RFC 8174 when, and only when, they appear in all capitals.

## Abstract
This document proposes **MCDF (Markdown Container Document Format)**, a modern, secure, AI-native document format designed to replace static PDF-centric workflows. MCDF treats documents as structured, versionable, cryptographically verifiable knowledge containers rather than fixed-layout files. It is optimized for collaboration, automation, long-term storage, and artificial intelligence processing, while still supporting high-fidelity rendering when required.

---

## 1. Motivation

PDFs were created for a printing-first world. Modern documents, however:
- Are consumed more by machines than printers
- Require fine-grained access control and auditability
- Must integrate with CI/CD, Git, and automation pipelines
- Serve as inputs to AI systems (LLMs, search, reasoning)

MCDF addresses these requirements by separating **content, structure, security, and presentation**, enabling documents to evolve as first-class digital assets.

---

## 2. Design Goals

MCDF is designed with the following principles:

1. **Content-first** – semantic meaning over layout
2. **AI-native** – predictable structure for LLMs and tools
3. **Git-friendly** – diffable, mergeable, versionable
4. **Secure by design** – encryption, signing, audit trails
5. **Composable** – modular reuse across systems
6. **Render-agnostic** – output to PDF, HTML, print, etc.
7. **Long-term durable** – readable without proprietary tools

---

## 3. High-Level Architecture

An MCDF document is a **container** holding multiple well-defined components.

```
example.mcdf/
├── content.md
├── schema.yaml
├── metadata.yaml
├── signatures/
│   └── author.sig
├── encryption/
│   └── policy.yaml
├── assets/
│   ├── image1.png
│   └── diagram.svg
├── audit.log
└── manifest.json
```

The container MAY be implemented as:
- Directory structure (authoring and Git-based workflows)
- TAR archive (RECOMMENDED interchange form; members MUST be ordered deterministically with normalized timestamps so identical content yields identical bytes)
- OCI-compatible artifact (registry-based distribution)
- ZIP (NOT RECOMMENDED: duplicate entries and local-header/central-directory ambiguities allow two parsers to see different content, complicating integrity verification)

Single-file containers SHOULD use the `.mcdf` file extension. A dedicated media type (e.g., `application/vnd.mcdf+tar`) is proposed for registration; see Open Questions.

---

## 4. Core Components

### 4.1 content.md

Primary human-readable document content.

- Markdown CommonMark compliant
- Extended with optional fenced blocks for math, diagrams, code
- No presentation-specific styling
- Asset references MUST use relative paths that resolve inside the container; renderers MUST NOT fetch external resources
- Section headings SHOULD carry stable identifiers (e.g., `# Contract Overview {#overview}`) matching the section `id`s declared in `schema.yaml`

Example:
```
# Contract Overview

This agreement is between **Party A** and **Party B**.
```

---

### 4.2 schema.yaml

Defines the logical structure of the document for machines.

Example:
```yaml
document_type: contract
sections:
  - id: overview
    title: Contract Overview
  - id: terms
    title: Terms and Conditions
  - id: signatures
    required: true
```

Purpose:
- Enables deterministic parsing
- Supports validation and AI reasoning
- Binds machine structure to content: every section `id` MUST correspond to a heading identifier in `content.md`, and validators MUST reject documents missing a section marked `required: true`

---

### 4.3 metadata.yaml

Descriptive and operational metadata.

```yaml
title: Master Service Agreement
version: 1.2.0
authors:
  - name: Alice
    id: did:key:z6Mk...
created_at: 2025-01-01
classification: confidential
language: en
```

---

### 4.4 manifest.json

Cryptographic root of trust.

```json
{
  "mcdf_version": "1.0",
  "hash_algorithm": "sha256",
  "files": {
    "content.md": "<hash>",
    "schema.yaml": "<hash>",
    "metadata.yaml": "<hash>",
    "encryption/policy.yaml": "<hash>",
    "assets/image1.png": "<hash>",
    "assets/diagram.svg": "<hash>"
  }
}
```

Used for:
- Integrity verification
- Signature validation
- Tamper detection

Rules:
- The manifest MUST list every file in the container except `manifest.json` itself, the contents of `signatures/`, and `audit.log`
- For encrypted files, the hash covers the ciphertext, so integrity is verifiable without decryption
- The manifest MUST be serialized in canonical form (RFC 8785 JSON Canonicalization Scheme) so hashing and signing are byte-deterministic
- `audit.log` is excluded because it grows after signing; its integrity is protected separately (Section 5.3)

---

## 5. Security Model

### 5.1 Digital Signatures

- Detached signatures stored under `signatures/`, one file per signer
- Supports multiple signers
- **Algorithm allow-list (v1, normative):** `EdDSA` (Ed25519) and `ES256`
  (ECDSA P-256). Verifiers MUST reject signatures using any other algorithm
  (`E_SIG_ALG_NOT_ALLOWED`). Widening the list (e.g., RSA-PSS or post-quantum
  ML-DSA) requires a revision of this specification.
- Signature encoding SHOULD be detached JWS (RFC 7515) or COSE_Sign1 (RFC 9052)
- Each signature SHOULD carry a key identifier resolvable through `metadata.yaml` (e.g., a DID), binding the signature to a declared author
- Countersignatures (e.g., an approver signing over an author) are additional signature files whose payload includes the hash of the prior signature

Example:
```
signatures/author.sig
```

Signatures MUST cover the canonical bytes of `manifest.json`, and thereby — transitively through the file hashes — every listed file in the container.

---

### 5.2 Encryption

Encryption is applied at the container or file level.

`encryption/policy.yaml`:
```yaml
method: aes-256-gcm
key_management:
  type: kms
  provider: external
access:
  - role: legal
    permission: read
```

Supports:
- Zero-trust access
- Attribute-based encryption
- Time-bound access

Notes:
- `policy.yaml` is declarative. Actual key material is wrapped per recipient (e.g., HPKE or KMS-managed keys) and MUST NOT be stored in plaintext in the container
- Attribute-based and time-bound access depend on the key-management layer: client-side cryptography alone cannot revoke access after a key has been released. Deployments requiring revocation MUST use an online KMS or policy-enforcement service

---

### 5.3 Audit Log

`audit.log` is append-only.

```
2025-01-10T10:00Z CREATED Alice
2025-01-12T15:30Z SIGNED Bob
```

Each entry MUST include the hash of the previous entry (hash chaining), making truncation and reordering detectable. Because the log grows after signing, it is excluded from `manifest.json`; a signed checkpoint of the latest entry hash MAY be recorded at release milestones.

MAY additionally be backed by:
- Merkle trees
- External notarization / transparency logs

---

## 6. Rendering & Transformation

MCDF explicitly separates **authoring** from **rendering**.

Supported outputs:
- PDF (for legal/printing)
- HTML/Web
- DOCX (legacy interop)
- Plain text

Rendering pipelines MUST be deterministic and reproducible: given the same container and the same renderer version, output bytes MUST be identical (fixed font embedding, no timestamps, no locale-dependent formatting). A renderer SHOULD emit the container hash and renderer version into the output's metadata so a rendered PDF can be traced back to its source container.

Presentation hints (page size, theme, print CSS) are OPTIONAL and live in a dedicated `presentation/` directory so that content remains presentation-free. Absence of presentation hints MUST yield a valid default rendering.

---

## 7. AI Integration Guidelines

MCDF is optimized for AI systems:

- Stable section IDs
- Explicit schema
- Machine-readable metadata
- Deterministic structure

Recommended AI operations:
- Summarization
- Clause extraction
- Compliance checks
- Semantic search
- Reasoning over versions

Provenance guidance: AI-generated or AI-modified content SHOULD be recorded in `audit.log` and MAY be marked in `metadata.yaml` (e.g., a `generated_by` field) so downstream consumers can distinguish human-authored from machine-authored sections. Signatures let a verifier establish which content was human-approved before any AI post-processing.

---

## 8. Versioning & Collaboration

- Semantic versioning recommended (the `version` field in `metadata.yaml`)
- Git-based workflows encouraged; the directory form is the canonical working representation, with the TAR/OCI form produced for release
- Conflict resolution at section level, aided by stable section IDs
- Because `manifest.json` embeds file hashes, a Git merge conflict in the manifest is an explicit, human-readable signal that underlying content diverged

PDF generation SHOULD occur only at release milestones.

---

## 9. Backward Compatibility

- PDFs can be embedded as assets
- MCDF containers can emit signed PDFs
- Hybrid workflows supported during transition

---

## 10. Conformance

### 10.1 Profiles (normative)

An implementation declares the highest profile it supports; each profile is a
superset of the one before it, and a fully useful reader/writer exists at
**Core** with no cryptography at all. Validating a container *at* a profile
applies that profile's checks and every check below it.

| Profile | An implementation MUST be able to… | Requires |
|---|---|---|
| **Core** | read, write and modify the members (`content.md`, `schema.yaml`, `metadata.yaml`, assets) and enforce structure binding (§4.2) | text + YAML + JSON |
| **Integrity** | build and verify `manifest.json` (§4.4) | + SHA-256, JCS (RFC 8785) |
| **Signed** | produce and verify detached signatures over the canonical manifest (§5.1) | + EdDSA / ES256, `did:key` |
| **Encrypted** | encrypt and decrypt members per `encryption/policy.yaml` (§5.2) | + AES-256-GCM, HPKE (RFC 9180) |
| **Render** | produce the deterministic HTML and plain-text renderings (§6) | + CommonMark renderer |

Validators MUST report failures using the stable error codes of the MCDF
conformance kit's error taxonomy (`/conformance/errors.md`), which is
normative; the known-answer vectors under `/conformance/vectors/` define the
expected verdicts an implementation is scored against.

### 10.2 Modification and re-signing semantics (normative)

- A Core implementation modifies a document by editing member files directly;
  no cryptography is required, and rebuilding `manifest.json` is OPTIONAL at
  Core.
- Adding or rebuilding `manifest.json`, adding signatures, or appending audit
  entries MUST NOT alter the bytes of any content member (`content.md`,
  `schema.yaml`, `metadata.yaml`, assets). Trust layers wrap content; they
  never rewrite it.
- Editing any listed member invalidates existing signatures by construction
  (its manifest hash changes). The modify flow is: edit → rebuild manifest →
  re-sign. A verifier MUST treat a container whose manifest does not match its
  members as failing Integrity regardless of signature validity.

### 10.3 Canonical form of text members (normative)

So that identical documents hash identically on every platform and editor:

- Line endings MUST be LF (U+000A); writers MUST normalize CR and CRLF to LF.
- A non-empty `content.md` MUST end with exactly one trailing LF (trailing
  blank lines are collapsed); an empty member stays empty.
- All other bytes are preserved verbatim. Trailing intra-line whitespace is
  significant in Markdown (hard line breaks) and MUST NOT be stripped.

The same rules apply to `schema.yaml` and `metadata.yaml`. Readers MUST
accept non-canonical line endings (the format stays forgiving), but any tool
that rewrites a member SHOULD write it back in canonical form. Hashes in
`manifest.json` always cover the stored bytes; canonical form is what makes
those bytes platform-independent.

---

## 11. Security Considerations

- **Content substitution**: prevented by manifest hashing plus signatures over the canonical manifest. A verifier MUST recompute every listed hash before trusting content.
- **Partial-file / mixed-version attacks**: because signatures cover the whole manifest, mixing files from different versions invalidates verification.
- **ZIP parser ambiguity**: see Section 3; TAR is RECOMMENDED for signed interchange.
- **Encrypted-content integrity**: hashes cover ciphertext, so tampering is detected before decryption, mitigating chosen-ciphertext and padding-oracle exposure at the container layer.
- **Audit-log tampering**: mitigated by hash chaining and optional external notarization; note the log is not covered by the primary signature.
- **Revocation limits**: client-side encryption cannot retract access to already-released keys (Section 5.2).
- **Rendering safety**: renderers MUST treat `content.md` and assets as untrusted input — no external fetches, no script execution, and sanitized HTML output — to avoid SSRF and injection when documents are rendered server-side.
- **Algorithm agility**: `hash_algorithm` and signature algorithms are declared, not fixed, so weak primitives can be retired; verifiers MUST reject algorithms not on their allow-list.

---

## 12. Licensing & Intellectual Property

### 12.1 Intent

MCDF is designed to be implementable by anyone, **royalty-free (RF)**, without dependence on proprietary technology. The specification, its reference implementation, and all contributions are governed by an explicit, permissive, royalty-free IP posture so that independent, interoperable implementations can be built without seeking permission or paying fees.

### 12.2 Licenses

MCDF uses a **dual-license split by content type**, adopted for this project (see `LICENSING.md` at the repository root):

- **Specification text** — this RFC and all specification documents — is licensed under the **Community Specification License 1.0** (`Community-Spec-1.0`; see the `LICENSE` file in this `spec/` directory). CSL is purpose-built for specifications: it grants everyone the right to build independent, interoperable implementations and binds Contributors to a royalty-free patent commitment with a formal patent-exclusion process (recorded in `NOTICE.md`).
- **Reference implementation and tooling** — all source code — is licensed under the **Apache License 2.0** (`Apache-2.0`; see `/LICENSE` at the repository root), whose Section 3 confers a perpetual, worldwide, royalty-free patent license from each contributor, with defensive termination.

### 12.3 Contributor Patent Commitment

- Every contribution MUST carry a royalty-free grant covering any patent claim that is necessarily infringed by implementing that contribution (an "Essential Claim").
- The grant is subject to **defensive termination**: a party that asserts a patent alleging that a Compliant Implementation infringes an Essential Claim forfeits the reciprocal grant.
- No contribution may impose royalty, field-of-use, per-seat, or discriminatory restrictions on Compliant Implementations.

### 12.4 Component Provenance

MCDF is composed exclusively of standards-track components published under royalty-free terms. Implementers SHOULD confirm that any additional component they introduce carries compatible RF terms **before** it becomes REQUIRED by a conformance profile. A component whose licensing would encumber Compliant Implementations MUST NOT be made mandatory.

### 12.5 Output-Format Conformance

When rendering to an external output format, implementers SHOULD target conformance with that format's published, royalty-free specification so that generated output remains within whatever patent grant that format's specification provides. Output that departs from a published RF profile of a target format falls outside the scope of MCDF's IP guarantees.

### 12.6 Scope of This Posture

This section governs MCDF's own specification and contributions. It is **not** a freedom-to-operate opinion for any particular product: implementers remain responsible for clearing patents relevant to the broader functional areas their product touches (e.g., rights management, versioning, collaboration).

---

## 13. Open Questions

- Standard container encoding for signed interchange (OCI vs TAR) and media-type registration
- Governance and stewardship (standards body, reference implementation, conformance suite)
- Post-quantum signature support (e.g., ML-DSA) alongside classical algorithms

Resolved in this revision: the minimum conformance profile (§10.1) and
`content.md` canonicalization (§10.3).

---

## 14. Conclusion

MCDF redefines documents as **secure, intelligent, composable knowledge containers**. It aligns documentation with modern software, security, and AI practices while preserving the ability to render traditional formats when necessary.

This RFC proposes MCDF as a foundation for the next generation of document systems.

---

## 15. References

### 15.1 Normative
- RFC 2119 / RFC 8174 — Requirement key words
- RFC 8785 — JSON Canonicalization Scheme (JCS)
- CommonMark Specification — `content.md` syntax
- RFC 7515 (JWS) / RFC 9052 (COSE) — signature encodings
- MCDF Conformance Kit (`/conformance` in the reference repository) — error taxonomy (`errors.md`), JSON Schemas, known-answer vectors

### 15.2 Informative
- Apache License 2.0 — reference implementation & patent grant
- Community Specification License 1.0 — candidate specification license
- OCI Image / Artifact Specification — registry distribution
- W3C Decentralized Identifiers (DID) — author/key identity
- NIST FIPS 186-5 — Digital Signature Standard (Ed25519, ECDSA, RSA)
- RFC 9180 — HPKE (recipient key wrapping)
- NIST FIPS 204 — ML-DSA (post-quantum signatures)

