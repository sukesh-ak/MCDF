<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 The MCDF Project -->

# Implementer's Guide

How to implement `.mcdf` in your language. Task-oriented; the RFC in [`/spec`](../spec)
is the normative reference, and [`errors.md`](errors.md) defines the error codes.

The single most important thing to know: **reading, writing and modifying a
document needs no cryptography.** A container is a directory (or TAR) of ordinary
files. Everything below Core is optional.

```
example.mcdf/
├── content.md          CommonMark. Headings carry {#id} anchors.
├── schema.yaml         document_type + sections[] (each id binds to a heading)
├── metadata.yaml       title, version, authors, classification, language
├── manifest.json       path -> sha256, the root of trust        (Integrity+)
├── signatures/*.sig    detached JWS over the canonical manifest  (Signed+)
├── encryption/policy.yaml                                        (Encrypted+)
├── assets/…            images, diagrams
└── audit.log           hash-chained history (excluded from the manifest)
```

## Core: read a document

1. List the files (directory walk, or untar).
2. Read `content.md` as UTF-8 CommonMark.
3. Parse `schema.yaml` / `metadata.yaml` as YAML.
4. Reject any member path that is absolute or contains `..` → `E_ASSET_PATH_ESCAPE`.

Bind structure to content by matching each `schema.yaml` section `id` to a
heading anchor in `content.md`:

```markdown
# Contract Overview {#overview}
```

A section id with no matching heading → `E_SCHEMA_UNBOUND`, or
`E_REQUIRED_SECTION_MISSING` when `required: true`.

## Core: modify a document

Edit the files. That's it. If the container has a manifest, rebuild it
(below). If it was signed, the signature is now invalid **by design** — re-sign
to publish a new version. Signing never rewrites `content.md`, so a Core writer
and a Signed signer never corrupt each other's work.

## Integrity: build and verify the manifest

`manifest.json` maps every member to its SHA-256:

```json
{"files":{"content.md":"d9d5…"},"hash_algorithm":"sha256","mcdf_version":"1.0"}
```

Membership: list every member **except** `manifest.json`, `audit.log`,
`audit.checkpoint`, and anything under `signatures/`. A present-but-unlisted file
→ `E_MANIFEST_EXTRA_FILE`; a listed-but-absent file → `E_MANIFEST_MISSING_FILE`;
a digest that differs → `E_MANIFEST_HASH_MISMATCH`.

**Hashes cover the bytes on disk.** For encrypted members that means ciphertext —
which is why integrity is verifiable without the decryption key.

Serialize the manifest in **RFC 8785 (JCS)** canonical form:
- object keys sorted by **UTF-16 code unit** (for ASCII paths this is plain byte order),
- no insignificant whitespace,
- minimal string escaping.

Check yourself against [`vectors/canonical/manifest`](vectors/canonical/manifest) —
your output must match `expected/manifest.json` byte-for-byte.

## Signed: verify a signature

Each `signatures/*.sig` is a **detached JWS** in compact form — the payload
segment is empty because the payload is the canonical manifest:

```
BASE64URL(header) .. BASE64URL(signature)
```

To verify:
1. Recompute the canonical manifest bytes → that is the payload.
2. Decode the header: `{"alg":"EdDSA","kid":"did:key:z6Mk…"}`.
3. Accept `alg` only from the allow-list: `EdDSA`, `ES256` → else `E_SIG_ALG_NOT_ALLOWED`.
4. Resolve `kid` (a `did:key`) to the public key — the did **is** the key:
   - `z6Mk…` = Ed25519 (multicodec `0xed01` + 32 raw bytes)
   - `zDn…` = ECDSA P-256 (multicodec `0x1200` + 33 compressed bytes)
   The key's type must match `alg` → else `E_SIG_ALG_MISMATCH`.
5. Signing input = `BASE64URL(header) + "." + BASE64URL(payload)`. Verify.
   ES256 signatures are raw `R||S` (64 bytes), not DER.

Because signatures cover the *canonical* manifest, reformatting
`manifest.json`'s whitespace does **not** invalidate them; changing a hash does.

Verifying a signature proves *who signed*, not *who is authorized*. Compare the
`kid` against the authors in `metadata.yaml` if you need authorization.

## Encrypted: decrypt

Read `encryption/policy.yaml`, find the recipient entry matching your key's
`did:key`, HPKE-open (`enc` + `wrapped_key`) to recover the content key, then
AES-256-GCM open each file in `encrypted_files`.

- Stored form per file: `nonce(12) || ciphertext || tag(16)`; **AAD = the member path**.
- HPKE: RFC 9180 base mode, DHKEM(X25519,HKDF-SHA256) / HKDF-SHA256 / AES-256-GCM,
  info = `mcdf/hpke/cek/v1`.
- No matching recipient → `E_NOT_A_RECIPIENT`; AEAD failure → `E_DECRYPT_FAILED`.

You can skip this entirely and still verify integrity and signatures.

## Packaging: the single-file form

A `.mcdf` file is a **USTAR** archive with normalized headers so it is
byte-deterministic: mtime/uid/gid `0`, mode `0644`, empty owner names, members
ordered by path, two zero blocks at the end. Any tar tool can read it.

## Audit log

`audit.log` is tab-separated, append-only, one entry per line:

```
timestamp \t action \t actor \t prev_hash
```

`prev_hash` is the SHA-256 of the previous entry's line (64 zeros for the first),
so edits, reordering and truncation all break the chain → `E_AUDIT_CHAIN_BROKEN`.
It is excluded from the manifest so history can grow after signing.

## Prove your implementation

```sh
./run.sh /path/to/your-cli
```

Expose `validate <container> --profile <p>` (exit 0 = valid) and
`manifest <container>` (canonical JSON on stdout), then score yourself against
the vectors. Structural output can also be checked with any JSON-Schema validator
using [`schemas/`](schemas).
