<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 The MCDF Project -->

# MCDF Conformance Kit

Everything you need to implement `.mcdf` **in any language** and prove your
implementation is correct — without reading the C++ reference runtime.

```
schemas/    JSON Schemas for manifest.json, metadata.yaml, schema.yaml, policy
vectors/    known-answer test vectors (valid/ canonical/ invalid/)
errors.md   the normative error-code taxonomy
GUIDE.md    task-oriented implementer's guide
run.sh      score any implementation's CLI against the vectors
```

## Conformance profiles

Implement only as far as you need. Each profile is a superset of the one above.

| Profile | Adds | Needs |
|---|---|---|
| **Core** | read / write / modify container, content, schema, metadata | text + YAML + JSON only |
| **Integrity** | build / verify `manifest.json` | + SHA-256, canonical JSON (RFC 8785) |
| **Signed** | detached JWS verify / produce | + Ed25519 or ECDSA P-256 |
| **Encrypted** | AES-256-GCM + HPKE unwrap | + AEAD / HPKE |
| **Render** | deterministic HTML / text | + a CommonMark renderer |

**Core needs no cryptography at all.** A container is just a directory (or TAR)
of Markdown + YAML + JSON, so a Core-profile reader/writer is a short program in
any language.

## Scoring an implementation

`run.sh` drives any CLI that exposes the MCDF verbs and reports which vectors
pass:

```sh
./run.sh                 # score the reference implementation (mcdf on PATH)
./run.sh /path/to/your-cli
```

It checks, per vector: that valid containers validate, that the canonical
manifest matches byte-for-byte, and that every invalid container is rejected.

## What is guaranteed byte-for-byte

Only three things depend on exact bytes; everything else is ordinary parsing:

1. **The canonical manifest** — RFC 8785 (JCS): object keys sorted by UTF-16
   code unit, no insignificant whitespace, minimal string escaping.
2. **The TAR packing** — normalized headers (mtime/uid/gid = 0, mode 0644, empty
   owner names), members ordered by path.
3. **`content.md` normalization** — LF line endings.

Signatures cover the *canonical* manifest, so reformatting `manifest.json`
whitespace does **not** break a signature; changing a hash does.
