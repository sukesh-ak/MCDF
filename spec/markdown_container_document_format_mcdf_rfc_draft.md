<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project. Licensed under the Community Specification License 1.0; see the LICENSE file in this directory. -->

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

The container has **one REQUIRED serialization and several OPTIONAL ones**.

**TAR archive — REQUIRED.** Every implementation MUST be able to read the TAR serialization, and an implementation that writes containers at all MUST be able to write it. Members MUST be ordered deterministically with normalized timestamps, so identical content yields identical bytes. This is the interchange form: the one form any two implementations are guaranteed to share, and the form the conformance kit scores.

**Directory structure — OPTIONAL.** The same members laid out as ordinary files in a folder. This is the authoring and Git-based workflow form, and the reason an MCDF document is diffable, reviewable and mergeable in version control; implementations that run on a general-purpose OS SHOULD support it. It is OPTIONAL because it is unreachable to an implementation with no filesystem — a reader streaming from flash on a microcontroller can never walk a directory — so it cannot be the form two implementations are guaranteed to share.

**These are the only two container forms.** An implementation encountering anything else is not looking at an MCDF container.

Two serializations, exactly one of them required, is what keeps *any implementation can open any document* true. A format that permits many forms and requires none permits two conforming implementations that cannot exchange a single file. The same split is used by EPUB (an OCF abstract container plus its ZIP serialization) and by OCI (`image-layout` plus the registry protocol): a logical model for working in, one named form for handing over.

Distribution mechanisms are not container forms. Carrying a document through an OCI registry, a ZIP bundle, an email attachment or a content-addressed store means transporting the `.mcdf` file: the transport wraps the interchange form rather than replacing it, and no MCDF parser needs to understand it. ZIP is additionally unsuitable as a container in its own right — duplicate entries and local-header/central-directory ambiguities let two parsers see different content from the same bytes, which is incompatible with hashing a container's members.

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

#### 4.1.1 Image layout hints (normative)

An image MAY carry layout hints in its CommonMark *title* slot:

```
![Site plan](assets/plan.png "width=600 align=center")
```

The title slot is used because it is the only place CommonMark already allows an
image to carry additional text. A document using hints therefore remains valid
CommonMark and renders in any Markdown tool; a tool that does not implement this
section simply shows the image at its natural size. No new syntax is introduced,
which is what keeps `content.md` portable.

Two hints are defined. Both are case-insensitive and appear as literal
`name=value` text with no whitespace around `=`:

| Hint | Value | Meaning |
|---|---|---|
| `width` | a positive number | Requested display width in CSS pixels |
| `align` | `left`, `center` or `right` | Horizontal placement within the content area |

Renderers implementing this section:

- MUST preserve the image's aspect ratio when `width` is applied; only the width
  is specified, and the height follows from it.
- MUST reduce an image to the available content width when the requested width
  exceeds it. `width` is a request, not a guarantee, so a hint can never force
  horizontal overflow.
- MUST ignore a hint whose value is absent, non-numeric, zero, negative, or (for
  `align`) outside the list above, and MUST render the image as though that hint
  were not present.
- MUST NOT present a title consisting of hints as a caption or tooltip; the hints
  are layout instructions, not prose.
- SHOULD treat a title containing no recognised hint as an ordinary CommonMark
  title.
- MUST NOT infer layout from any other source (file name, dimensions, surrounding
  markup).

Hints are presentation, and presentation is advisory: they MUST NOT affect
`manifest.json`, canonicalization (§10.3), or any signature. Two documents
differing only in hint text are different documents by digest, because the hints
live in `content.md` like any other text — but a renderer that ignores §4.1.1
entirely still produces a correct, complete rendering.

> Non-goals for v1: captions, floats with text wrap, per-output-format sizing,
> and units other than pixels. These need layout semantics MCDF does not yet
> define, and adding them later cannot invalidate documents written against this
> section.

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
- Binds machine structure to content: every section `id` MUST correspond to a heading identifier in `content.md`, and validators MUST reject documents missing a section marked `required: true`. When `content.md` is sealed (§5.2) this binding is evaluated against the structure attestation instead of the headings — see §5.2.1

Fields:

| Field | Requirement | Meaning |
|---|---|---|
| `document_type` | OPTIONAL, RECOMMENDED | Machine-readable document kind |
| `sections` | OPTIONAL | Ordered list of logical sections |
| `sections[].id` | REQUIRED within a section | Binds to a `{#id}` heading anchor; MUST match `^[A-Za-z0-9._-]+$` |
| `sections[].title` | OPTIONAL | Human-readable section name |
| `sections[].required` | OPTIONAL, default `false` | A missing heading raises `E_REQUIRED_SECTION_MISSING` |

`document_type` is **optional but strongly recommended**. It is what lets tooling
classify a document without reading it — pick a template, route for review,
decide what an automated agent may do with it — and a document that omits it
gives up all of that. It is nevertheless not required, because MCDF's structural
guarantees come from `sections[]`, and a document with no meaningful type should
be free to say nothing rather than be forced to invent one.

`document_type` MUST NOT be present and empty. Absence is how a document declines
to state a type; `document_type: ""` is a third state that means the same thing
while looking like a value, and writers MUST omit the key instead. Values are
free-form strings — `contract`, `report`, `memo`, `invoice`, `book` are examples,
not an enumeration, and MCDF defines no registry of them.

`schema.yaml` may be omitted from a container entirely. A document with no schema
makes no structural claims, which is a valid thing for a document to do, and is
the right choice over shipping an empty schema.

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

Encryption is applied per member, not to the container as a whole: a document
can keep its metadata and structure readable while sealing its content.

`encryption/policy.yaml` (normative shape):
```yaml
method: aes-256-gcm
key_management: hpke
encrypted_files:
  - content.md
structure:
  bound_sections:
    - overview
    - terms
recipients:
  - id: did:key:z6LSerLwKkreS9zTk25eKt3eXC32Bm7mqsahk3z6WYHSDFBm
    enc: RsuLsd-7cjQF5O7rHTARzHWrszGLpG_tWZnacR_4gDI
    wrapped_key: uP1Tx2EfiDMbl3MOmBN6SGhKQdUOtOK_8TRPndltZD5Dr_D1C7PKKNyb56j-POdi
```

| Field | Requirement | Meaning |
|---|---|---|
| `method` | REQUIRED | Content-encryption algorithm. v1 allow-list: `aes-256-gcm` |
| `key_management` | REQUIRED | How the content-encryption key is wrapped. v1 allow-list: `hpke` |
| `encrypted_files` | REQUIRED, non-empty | Container-relative paths whose stored bytes are ciphertext |
| `structure` | CONDITIONAL | Structure attestation; see §5.2.1 |
| `recipients` | REQUIRED, non-empty | One entry per party that can unwrap the content key |
| `recipients[].id` | REQUIRED | Recipient's X25519 `did:key` |
| `recipients[].enc` | REQUIRED | base64url HPKE encapsulated key |
| `recipients[].wrapped_key` | REQUIRED | base64url HPKE-sealed content-encryption key |

A member listed in `encrypted_files` is **sealed**. Its stored bytes are the
AEAD output and MUST NOT be interpreted as the member's usual type; readers MUST
NOT parse, render or rewrite a sealed member.

Rules:
- One content-encryption key seals every listed member; that key is wrapped
  separately for each recipient. Key material MUST NOT appear in plaintext in
  the container.
- The AEAD's additional authenticated data MUST be the member's container path,
  so a ciphertext relocated to a different member fails authentication rather
  than decrypting somewhere it does not belong.
- `manifest.json` hashes the **ciphertext** — the bytes actually stored — so
  Integrity and Signed hold over a sealed document, and tampering is detected
  before any decryption is attempted (§11).
- Sealing or unsealing a member changes its stored bytes and therefore its
  manifest hash: the flow is edit → seal → rebuild manifest → sign. Signing
  before sealing produces a signature that no longer verifies.
- Attribute-based and time-bound access depend on the key-management layer:
  client-side cryptography alone cannot revoke access after a key has been
  released. Deployments requiring revocation MUST use an online KMS or
  policy-enforcement service.

#### 5.2.1 Structure attestation (normative)

`schema.yaml` binds each declared section to a `{#id}` heading in `content.md`
(§4.2). When `content.md` is sealed, a party without the key cannot see those
headings — so without help, a validator must either skip the check or report a
required section as *missing* when it is merely unreadable. Neither is
acceptable: the first quietly reduces what "valid" means, the second makes
encrypting a document cost it its conformance.

The encryptor therefore records the binding result it *could* see, at the moment
it sealed the content:

```yaml
structure:
  bound_sections: [overview, terms]
```

- `structure.bound_sections` lists exactly those `schema.yaml` section `id`s
  that had a matching heading anchor in the plaintext `content.md` at seal time.
  It MUST NOT list anything else; in particular it MUST NOT reveal headings that
  do not correspond to a declared section.
- An encryptor MUST write `structure` when it seals `content.md` and the
  container has a `schema.yaml` that is readable (present and not itself sealed)
  and declares at least one section. Omitting it in that case raises
  `E_POLICY_INVALID`.
- An encryptor MUST NOT write `structure` when `schema.yaml` is absent or
  sealed. The section `id`s are only safe to publish because `schema.yaml`
  already publishes them; if the schema is itself confidential, the attestation
  would leak it.
- A validator evaluating §4.2 against a sealed `content.md` MUST use
  `bound_sections` in place of the headings, applying the same rule: a section
  marked `required: true` that is absent from the list raises
  `E_REQUIRED_SECTION_MISSING`, and any other absent section raises
  `E_SCHEMA_UNBOUND`.
- An implementation that decrypts a document MUST re-evaluate the binding
  against the recovered headings, and MUST raise
  `E_STRUCTURE_ATTESTATION_INVALID` if the result differs from `bound_sections`.

**What this is, and what it is not.** The attestation is a *statement by the
encryptor*, not a proof a third party can recompute — a hostile encryptor can
write a list that does not match the content it sealed. Three things constrain
it, and implementers should understand all three:

1. It is covered by `manifest.json`, and therefore by every signature over that
   manifest, so it cannot be altered after the fact and it is attributable to
   whoever signed.
2. It is checked by the first party who opens the document. A false attestation
   does not survive contact with a recipient.
3. It reveals nothing a reader did not already have: the `id`s come from a
   `schema.yaml` the same reader can read.

A commitment that a non-recipient could verify — a hash of the plaintext, say —
is deliberately **not** used. It would turn every sealed document into a
confirmation oracle: an attacker who can guess the content can hash a candidate
and compare, which is a real attack on the short, templated documents MCDF is
most often used for. See §11.

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

**Sealed members cannot be rendered.** A renderer MUST NOT treat a sealed member
(§5.2) as its usual type; feeding ciphertext to a CommonMark parser produces
plausible-looking nonsense rather than an error, which is worse than refusing.
An implementation asked to render a document whose `content.md` is sealed MUST
fail with `E_CONTENT_SEALED` rather than emit output, and a validator MUST NOT
evaluate this section's renderings for a sealed member — the Render profile is
not applicable to a document that has not been opened.

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
- Git-based workflows encouraged; the directory form is the canonical working representation, with the TAR form produced for release
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

**Container serialization is orthogonal to the profile ladder, and is not
optional.** At every profile, an implementation MUST be able to read the TAR
serialization (§3), and MUST be able to write it if it writes containers at
all; the directory form is OPTIONAL. Profiles say how much of a
document's trust machinery you evaluate; serialization says whether you can
open the file at all. An implementation that reads only directories is
therefore not conforming at *any* profile — not because it checks too little,
but because no other implementation can hand it a document. Vectors are
published in both forms and MUST be scored in the TAR form.

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

### 10.4 Canonical render (normative)

§6 requires a renderer to be reproducible against itself. That is not enough to
be *checked*: an implementation can only be scored on the Render profile if the
expected bytes are the same for everyone, so this section fixes them. Two
renderings are canonical — HTML and plain text — and a conforming Render
implementation MUST produce exactly the bytes described here for the
`canonical/render` vector of the conformance kit.

**The canonical render identifies the format, not the renderer.** The HTML
carries `<meta name="generator" content="mcdf-render/2">`, where the number is
the version of *this section*, and MUST NOT carry the name or version of the
implementation that produced it. A renderer that stamps its own version makes its
output unreproducible by anyone else, which forfeits the only property this
section exists to provide. Provenance is carried by `mcdf-source` below, which
identifies the container and is the same number whoever computes it.

**Parsing.** Parse `content.md` as CommonMark with **no extensions** — no tables,
task lists, strikethrough or footnotes; a document that wants them is asking for
a rendering MCDF does not define.

**Heading anchors become `id` attributes.** A heading carrying a `{#id}` anchor
(§4.1) renders as `<h2 id="overview">Contract Overview</h2>`: the anchor is
removed from the heading's text — it is structure, not prose — and emitted as the
attribute, escaped as any attribute value is. Two rules keep implementations from
diverging here:

- A heading **without** an anchor gets **no `id`**. Renderers commonly generate
  one by slugifying the heading text; a canonical render MUST NOT, because no two
  slugifiers agree and the resulting bytes would differ everywhere.
- Anchors are emitted **as written**, in document order, with no de-duplication
  and no normalisation. A repeated anchor is an authoring error and stays one.

Determine which headings carry anchors **from the parse**, never by scanning the
source for lines beginning with `#`. A line scan cannot distinguish a heading
from a line inside a fenced code block, and it misses every heading that does not
begin its line — any heading inside a blockquote or list item — and every setext
heading.

**Raw HTML is text, not markup.** `<div>x</div>` in `content.md` is neither
emitted nor recognised as an HTML block: those bytes are ordinary characters that
happen to look like markup, so they become a paragraph and are escaped like any
other text. Escaping raw HTML while still treating it as a *block* is a different
and equally safe reading, and implementations were observed doing both — hence
this sentence.

**HTML serialization** follows the CommonMark reference (what `cmark` prints, and
what the CommonMark specification's own examples show), so that an implementation
using any spec-compliant library matches without post-processing:

- void elements are self-closed: `<br />`, `<hr />`, `<img … />`;
- every block-level element starts at the beginning of a line, including inside a
  list item — `<li>\n<p>a</p>\n</li>`, and `<li>one\n<ul>` for a nested list;
- character references are decoded (`&copy;` renders as the character), and `&`,
  `<`, `>` and `"` are escaped wherever they occur in text or attribute values.

Image layout hints (§4.1.1) are applied here: the hint is removed from the
`title` attribute — it is an instruction, not a caption — and replaced by
`width` and `style` attributes. A `title` carrying no recognised hint is kept.

**The HTML document** is exactly:

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; img-src data:;">
<title>TITLE</title>
<meta name="generator" content="mcdf-render/2">
<meta name="mcdf-source" content="sha256:HASH">
<style>…</style>
</head>
<body>
BODY</body>
</html>
```

where `TITLE` is `metadata.yaml`'s title (HTML-escaped; `MCDF Document` when
absent, unreadable or empty), `HASH` is the SHA-256 of the canonical manifest
bytes (§5.1) — the whole `mcdf-source` line is omitted when the container has no
`manifest.json` — and the `<style>` is the fixed declaration in the vector. The
CSP is part of the canonical bytes, not decoration: it is what stops a rendered
document reaching the network (§11).

**Plain text** is a text extraction, not a styled rendering. Each paragraph,
heading, list item and code block becomes one block; each block is trimmed of
leading and trailing whitespace, empty blocks are dropped, and the remainder are
joined by one blank line, with a single trailing newline at the end. Inside a
block, a soft line break becomes a single space and a hard line break a newline;
code blocks keep their internal newlines. Link destinations, image destinations
and a code fence's info string are syntax and do not appear; link text, image alt
text and autolink URLs are prose and do.

Character references are **not** decoded in the plain-text render: `&copy;` stays
`&copy;`. Decoding would oblige every implementation to carry the HTML5
named-character-reference table — some two thousand entries — to produce plain
text. The HTML render decodes them because its CommonMark library already must.

**Version history.** Render **1** dropped heading anchors entirely, which left a
rendered document unable to express its own structure — a reader could not link
to the section a validator had just checked. Render **2** carries them. The
version exists so a change like that is a stated one: an implementation reads the
number and knows which bytes to produce.

**A sealed `content.md` has no canonical render** (§6). An implementation asked
for one MUST fail with `E_CONTENT_SEALED`, and a validator MUST treat the Render
profile as not applicable rather than failed — the document is not defective, it
is closed.

---

## 11. Security Considerations

- **Content substitution**: prevented by manifest hashing plus signatures over the canonical manifest. A verifier MUST recompute every listed hash before trusting content.
- **Partial-file / mixed-version attacks**: because signatures cover the whole manifest, mixing files from different versions invalidates verification.
- **ZIP parser ambiguity**: not a container form (§3). ZIP's duplicate entries and local-header/central-directory disagreement let two parsers read different content from one file, so a manifest hash could verify for one reader and not another. TAR is the required interchange form precisely because it has no second index to disagree with.
- **Encrypted-content integrity**: hashes cover ciphertext, so tampering is detected before decryption, mitigating chosen-ciphertext and padding-oracle exposure at the container layer.
- **No plaintext commitments in a sealed container**: nothing outside the ciphertext may commit to the plaintext of a sealed member — no plaintext hash, no length-revealing digest, no per-section proof a non-recipient could test. MCDF documents are frequently short and templated, so an attacker who can guess the content could otherwise confirm the guess offline against the commitment. This is why the structure attestation (§5.2.1) lists section `id`s the reader already holds from `schema.yaml`, rather than proving anything about the sealed bytes, and why its trustworthiness rests on signature attribution and on verification at decrypt time instead.
- **Sealed members are opaque**: a sealed member's bytes MUST NOT be parsed, rendered or rewritten as its usual type (§5.2, §6). A CommonMark parser handed ciphertext does not fail — it produces confident nonsense — and an editor that writes a text buffer back over a sealed member destroys the only copy.
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

- Media-type registration for the `.mcdf` interchange form (`application/vnd.mcdf+tar`). Registering it is also what would let a document travel through an OCI registry as an artifact without inventing a per-tool convention — the registry carries the interchange file as a layer blob, which needs a media type but no changes to this specification
- Governance and stewardship (standards body, reference implementation, conformance suite)
- Post-quantum signature support (e.g., ML-DSA) alongside classical algorithms

Resolved in an earlier revision: the minimum conformance profile (§10.1) and
`content.md` canonicalization (§10.3).

Resolved in this revision: the normative shape of `encryption/policy.yaml`
(§5.2, which previously showed an illustrative KMS example no implementation
wrote), how structure binding is evaluated when `content.md` is sealed
(§5.2.1), and **which container serializations exist and which one an
implementation must support** (§3, §10.1).

The last closed a real interoperability hole: four container forms were
permitted and none was required, so two conforming implementations could share
no form at all and still be unable to exchange a document. The list is now two
— TAR (REQUIRED) and directory (OPTIONAL) — and OCI artifacts and ZIP are no
longer container forms, since a registry or a bundle transports the interchange
file rather than replacing it.

Left open by §5.2.1: the attestation is verifiable by a recipient but not by a
third party, by deliberate choice (a non-recipient-verifiable commitment would
be a confirmation oracle — §11). A zero-knowledge proof of binding would close
that without the oracle, and is worth revisiting if the cost ever justifies it.

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
- OCI Image / Artifact Specification — registry distribution of the interchange file (transport only; not a container form, §3)
- W3C Decentralized Identifiers (DID) — author/key identity
- NIST FIPS 186-5 — Digital Signature Standard (Ed25519, ECDSA, RSA)
- RFC 9180 — HPKE (recipient key wrapping)
- NIST FIPS 204 — ML-DSA (post-quantum signatures)

