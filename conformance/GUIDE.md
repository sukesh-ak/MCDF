<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# Implementer's Guide

How to implement `.mcdf` in your language. Task-oriented; the RFC in [`/spec`](../spec)
is the normative reference, and [`errors.md`](errors.md) defines the error codes.

The single most important thing to know: **reading, writing and modifying a
document needs no cryptography.** A container is a set of ordinary files.
Everything below Core is optional.

There are exactly two container forms, and the one that is not optional is
**TAR** — every implementation must read it, whatever profile it stops at
(spec §3). The **directory** form is a convenience for authoring and git;
support it if it helps you, but a tool that speaks only directories cannot be
handed a document by anyone else. Nothing else is a container: shipping a
document through a registry or inside a ZIP means carrying the `.mcdf` file,
which needs no parser support from you.

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

1. List the files (untar; or a directory walk if you support that form).
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

**The heading must be top-level** (§4.2). An anchor inside a block quote, list
item or table cell does not bind — for binding, that heading is not there. This
is deliberately generous to you: finding top-level headings is a line scan, so
you can reach Core with no CommonMark parser at all and still agree with every
other implementation. Watch two things if you scan lines rather than parse:
skip fenced and indented code blocks, and remember that setext headings
(`Title {#id}` underlined with `===` or `---`) are headings too.

If you render, you need a real parse anyway — and there *every* heading counts,
nested ones included (§10.4).

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

### The structure attestation

A sealed `content.md` has no headings, so `schema.yaml`'s section binding (§4.2)
cannot be checked without the key. Rather than skip the check or blame the
document, the encryptor records what it saw before sealing:

```yaml
structure:
  bound_sections: [overview, terms]
```

Two obligations fall out of that, and both are easy to miss:

- **Writing.** If you seal `content.md` and the container has a readable
  `schema.yaml` with sections, you MUST write this list — compute it *before*
  encrypting, or the headings are gone. Omit it when `schema.yaml` is absent or
  itself sealed; publishing the ids of a schema you just hid defeats the point.
- **Reading.** Validate §4.2 against `bound_sections` instead of the headings,
  raising the same `E_REQUIRED_SECTION_MISSING` / `E_SCHEMA_UNBOUND`. If you
  decrypt, re-check against the recovered headings and raise
  `E_STRUCTURE_ATTESTATION_INVALID` on a mismatch — that check is the only thing
  standing between an attestation and a wish.

Do **not** invent a stronger commitment (a plaintext hash, say). A non-recipient
who can verify it can also test guesses against it, and MCDF documents are often
short and templated. See spec §5.2.1 and §11.

## Render: the canonical HTML and text

The Render profile is the one where an implementation is scored on **exact
bytes**, so the output is fixed by spec §10.4 rather than left to your Markdown
library's taste. Check yourself against
[`vectors/canonical/render`](vectors/canonical/render): `render html` and
`render text` must match `expected/render.html` and `expected/render.txt`
byte-for-byte. That vector is deliberately built out of the constructs where two
CommonMark libraries were caught disagreeing.

The parts most likely to catch you out:

- **Do not stamp your name or version into the output.** The generator meta says
  `mcdf-render/2` — the version of the render format, not of your tool. Output
  that identifies its producer cannot be reproduced by anyone else, which would
  make the profile unscoreable.
- **`{#id}` anchors become `id` attributes** — `<h2 id="overview">Contract
  Overview</h2>` — and the anchor comes out of the heading text. Take the anchors
  from your *parser*, not by scanning lines that start with `#`: a line scan
  edits code samples that contain a `#` line and misses every heading in a
  blockquote or list, plus every setext heading. Do **not** generate an id for a
  heading that has no anchor; slugifying is what most libraries do by default and
  no two do it the same way.
- **Self-close void elements** (`<br />`, `<hr />`, `<img … />`) and **start every
  block on its own line**, including inside a list item (`<li>\n<p>a</p>`,
  `<li>one\n<ul>`). This is what `cmark` prints and what the CommonMark
  specification's examples show, so a spec-compliant library gives it to you free.
- **Raw HTML is text.** `<div>x</div>` becomes a paragraph of escaped characters,
  not an escaped HTML *block*. If your library escapes raw HTML but still treats
  it as a block, disable the construct instead.
- **No extensions.** No tables, task lists, strikethrough or footnotes.
- **The plain text is an extraction**: blocks trimmed and joined by a blank line;
  soft breaks become a space, hard breaks a newline; link and image destinations
  and a fence's info string are syntax and do not appear; character references
  are left **undecoded** (`&copy;` stays `&copy;`) so you never need the HTML5
  entity table.

A sealed `content.md` has no render at all: fail with `E_CONTENT_SEALED`, and
treat the profile as not applicable when validating rather than reporting a
failure — see [`vectors/valid/render-sealed`](vectors/valid/render-sealed).

## Packaging: the required form

A `.mcdf` file is a **USTAR** archive with normalized headers so it is
byte-deterministic: mtime/uid/gid `0`, mode `0644`, empty owner names, members
ordered by path, two zero blocks at the end. Any tar tool can read it.

This is the serialization every implementation must support, and the one the
kit scores by default. It is deliberately the cheapest possible container: a
~100-line walk with no central directory, no compression and no allocation, so
it stays reachable from a microcontroller with a byte-range callback and no
filesystem — which is exactly the property the directory form cannot have.

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
./run.sh /path/to/your-cli          # POSIX, scores the required tar form
.\run.ps1 C:\path\to\your-cli       # Windows
./run.sh /path/to/your-cli dir      # add this if you support directories too
```

Expose `validate <container> --profile <p>` (exit 0 = valid), `manifest
<container>` (canonical JSON on stdout) and, if you claim Render, `render
<html|text> <container>` (the canonical render on stdout), then score yourself
against the vectors. Structural output can also be checked with any JSON-Schema
validator using [`schemas/`](schemas).

**Stopping partway is expected.** For a profile or verb you do not implement,
exit non-zero with `E_UNIMPLEMENTED` in the output. The runner records that
vector `N/A` instead of failing it and prints a `not implemented` count
alongside the score, so you get a clean result for the profiles you claim
without the ones you don't looking like defects. What you must never do is
report a profile as valid without running its checks — see
[`errors.md`](errors.md).

### If you are writing a reader

The runner scores a CLI, which assumes a machine that can host one. A read-only
implementation — an embedded reader, a viewer, anything that opens documents and
never writes them — is conforming or not for narrower reasons, and this is the
whole of it:

1. **Open every container under [`vectors/valid/`](vectors/valid).** Index it,
   list its members, and read each member's bytes. A valid document your reader
   cannot open is a defect in the reader, whatever profile it claims.
2. **Return the same bytes.** A member read must be byte-identical to the
   member's stored content, at any offset and any length inside it. A partial
   read is never reported as a success.
3. **Refuse every container under [`vectors/invalid/`](vectors/invalid) without
   crashing.** Report the code [`errors.md`](errors.md) names for the defect
   where it names one, and some error where it does not. What is never
   acceptable is accepting one.
4. **Read nothing outside the container.** Not one byte past the end, past a
   member's length, or past a caller's buffer — including on the malformed
   inputs of (3), where a size field reaching past the end of the archive is the
   whole point of the vector. On a part with no MMU this is a silent corruption
   rather than a crash, so it cannot be left to a segfault to catch.
5. **Never claim a profile you did not evaluate.** Report `E_UNIMPLEMENTED`, as
   above. A reader with no cryptography is a perfectly conforming Core
   implementation; one that reports Integrity as valid without hashing anything
   is not.

Points (3) and (4) are what a test suite tends to under-cover, because valid
documents are the ones people have. Fuzz them instead of enumerating them: the
repository's [corpus](../fuzz/corpus) seeds a coverage-guided run straight at
the archive walk and the member parsers, and it is the same corpus the reference
implementations are fuzzed from.

> **Maintainers: the two runners are a pair.** `run.sh` and `run.ps1` perform
> the same checks and must stay in step — a check added to one has to be added
> to the other, or an implementation scores differently depending on who runs
> it. Both are registered with ctest, so `ctest` catches a runner that has
> stopped agreeing with the vectors, but nothing detects a check that exists in
> only one of them. Change them together.
