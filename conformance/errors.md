<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

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
| `E_SIG_MISSING` | validating the Signed profile and the container has no `signatures/*.sig` |
| `E_SIG_INVALID` | a signature does not verify over the canonical manifest |
| `E_SIG_ALG_NOT_ALLOWED` | JWS `alg` is outside the allow-list (`EdDSA`, `ES256`) |
| `E_SIG_ALG_MISMATCH` | the resolved key's type does not match the header `alg` |
| `E_KID_UNRESOLVABLE` | `kid` (or a policy recipient `id`) is not a resolvable `did:key` |

## Encrypted profile

Static validation (no key supplied) checks `encryption/policy.yaml` soundness;
an unencrypted container trivially passes. The two decrypt-time codes require a
key and are raised by the decrypt operation, not `validate`.

| Code | Raised when |
|---|---|
| `E_POLICY_INVALID` | `encryption/policy.yaml` is unparseable or inconsistent (no files, missing listed files, no/incomplete recipients, or a required structure attestation is absent — spec §5.2.1) |
| `E_NOT_A_RECIPIENT` | no recipient entry matches the supplied key (decrypt) |
| `E_DECRYPT_FAILED` | AEAD authentication fails (decrypt) |
| `E_STRUCTURE_ATTESTATION_INVALID` | the recovered headings do not match `structure.bound_sections` (decrypt) |
| `E_CONTENT_SEALED` | a renderer or parser was asked to treat a sealed member as its usual type (spec §6) |

### Structure binding over sealed content

`schema.yaml` binds sections to `{#id}` headings in `content.md`. When
`content.md` is sealed, those headings are unreadable, so the encryptor records
which sections bound at seal time (spec §5.2.1):

```yaml
structure:
  bound_sections: [overview, terms]
```

Validators evaluate §4.2 against that list instead of the headings, raising the
**same** codes — `E_REQUIRED_SECTION_MISSING` and `E_SCHEMA_UNBOUND` — so the
rule a document is judged by does not change when it is encrypted. An
implementation that decrypts MUST re-check against the real headings and raise
`E_STRUCTURE_ATTESTATION_INVALID` on a mismatch.

The attestation is REQUIRED when `content.md` is sealed and a readable
`schema.yaml` declares sections, and MUST NOT be present when `schema.yaml` is
absent or itself sealed.

## Audit

| Code | Raised when |
|---|---|
| `E_AUDIT_CHAIN_BROKEN` | an entry's `prev_hash` does not match the previous entry's hash |
| `E_AUDIT_CHECKPOINT_INVALID` | the checkpoint signature fails, or the chain to its head is broken |

## Partial implementations (non-normative)

An implementation that does not support a profile has three options, and only
one of them is honest.

| Behaviour | Verdict |
|---|---|
| Report the profile as valid without running its checks | **Never.** It scores a false pass and tells a user their document is trustworthy on the strength of a check that did not happen. |
| Fail with a profile-specific code (`E_SIG_INVALID`, …) | Misleading. It blames the document for the implementation's gap. |
| Fail with `E_UNIMPLEMENTED` | Correct. |

`E_UNIMPLEMENTED` is therefore **reserved**: it means "this implementation did
not evaluate this profile", never "the document is wrong". It is deliberately
not normative — no `invalid/` vector may require it, and no implementation is
obliged to emit it — but the kit reserves the name so a scoring harness can tell
*absent* from *wrong*.

`run.sh` and `run.ps1` act on it: a vector whose run exits non-zero with
`E_UNIMPLEMENTED` in its output is recorded `N/A` rather than passed or failed,
and the summary line grows a `not implemented` count. The count is always
printed — a score of "0 failed" means something different when a third of the
vectors were never evaluated. Everything else the implementation says is scored
normally, so declining a profile buys a clean score for the profiles it does
claim and nothing more.

Observed first in `mcdf-ts`, which reported `E_UNIMPLEMENTED` for Render while a
canonical render had to be byte-identical to the reference output and there was
nothing published to prove that against. Both halves of that are closed — spec
§10.4 fixes the bytes and `canonical/render` publishes them — and `mcdf-ts`
claims all five profiles. The code is now emitted by `mcdf_micro`, the C reader,
which implements Core and Integrity and will never implement the rest: it holds
no private keys and never decrypts, so Signed above verification and Encrypted
are not gaps it intends to close. That is the case the reservation was made for.

## Manifest membership (normative)

The manifest MUST list every container member **except**:

- `manifest.json` itself
- `audit.log` and `audit.checkpoint` (they grow/change after signing)
- anything under `signatures/`

Anything else present but unlisted MUST raise `E_MANIFEST_EXTRA_FILE`.
