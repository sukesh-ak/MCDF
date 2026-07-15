<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 The MCDF Project -->

# MCDF Error Taxonomy (normative)

Stable error codes so conformance is objective: every `invalid/` vector names
the code an implementation MUST raise. Messages are free-form; **codes are not**.

## Structure (Core profile)

| Code | Raised when |
|---|---|
| `E_MISSING_CONTENT` | `content.md` is absent |
| `E_SCHEMA_UNBOUND` | a `schema.yaml` section `id` has no matching heading `{#id}` in `content.md` |
| `E_REQUIRED_SECTION_MISSING` | a section marked `required: true` has no matching heading |
| `E_ASSET_PATH_ESCAPE` | a member path is absolute or contains `..` |

## Integrity profile

| Code | Raised when |
|---|---|
| `E_MISSING_MANIFEST` | `manifest.json` is absent |
| `E_MANIFEST_HASH_MISMATCH` | a listed file's recomputed hash differs |
| `E_MANIFEST_MISSING_FILE` | a file listed in the manifest is absent |
| `E_MANIFEST_EXTRA_FILE` | a non-excluded file is present but unlisted |
| `E_ALGO_NOT_ALLOWED` | `hash_algorithm` is outside the allow-list (`sha256`) |

## Signed profile

| Code | Raised when |
|---|---|
| `E_SIG_INVALID` | a signature does not verify over the canonical manifest |
| `E_SIG_ALG_NOT_ALLOWED` | JWS `alg` is outside the allow-list (`EdDSA`, `ES256`) |
| `E_SIG_ALG_MISMATCH` | the resolved key's type does not match the header `alg` |
| `E_KID_UNRESOLVABLE` | `kid` is not a resolvable `did:key` |

## Encrypted profile

| Code | Raised when |
|---|---|
| `E_NOT_A_RECIPIENT` | no recipient entry matches the supplied key |
| `E_DECRYPT_FAILED` | AEAD authentication fails |

## Audit

| Code | Raised when |
|---|---|
| `E_AUDIT_CHAIN_BROKEN` | an entry's `prev_hash` does not match the previous entry's hash |
| `E_AUDIT_CHECKPOINT_INVALID` | the checkpoint signature fails, or the chain to its head is broken |

## Manifest membership (normative)

The manifest MUST list every container member **except**:

- `manifest.json` itself
- `audit.log` and `audit.checkpoint` (they grow/change after signing)
- anything under `signatures/`

Anything else present but unlisted MUST raise `E_MANIFEST_EXTRA_FILE`.
