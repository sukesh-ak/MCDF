<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# MCDF Conformance Kit

Everything you need to implement `.mcdf` **in any language** and prove your
implementation is correct — without reading the C++ reference runtime.

```
schemas/    JSON Schemas for manifest.json, metadata.yaml, schema.yaml, policy
vectors/    known-answer test vectors (valid/ canonical/ invalid/)
errors.md   the normative error-code taxonomy
GUIDE.md    task-oriented implementer's guide
run.sh      score any implementation's CLI against the vectors (POSIX sh)
run.ps1     the same, for Windows (PowerShell 5.1+)
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

**Core needs no cryptography at all.** A container is Markdown + YAML + JSON, so
a Core-profile reader/writer is a short program in any language.

**There are two container forms, and one of them is required.** Every
implementation MUST read the **TAR** form (spec §3) — it is what any two
implementations are guaranteed to share. The **directory** form is OPTIONAL:
it is what makes a document diffable in git, and it is unreachable to an
implementation with no filesystem, so it cannot be the common ground. There is
no third form; a registry or a bundle transports the `.mcdf` file rather than
replacing it. Every vector is published in both forms, `container/` and
`container.mcdf`.

## Scoring an implementation

`run.sh` drives any CLI that exposes the MCDF verbs and reports which vectors
pass:

```sh
./run.sh                     # score the reference implementation (mcdf on PATH)
./run.sh /path/to/your-cli   # the REQUIRED tar form
./run.sh /path/to/your-cli dir   # also score the OPTIONAL directory form
```

On Windows, `run.ps1` is the equivalent and is kept in step with it — same
checks, same output, same exit codes:

```powershell
.\run.ps1                      # score the reference implementation (mcdf on PATH)
.\run.ps1 C:\path\to\your-cli
.\run.ps1 C:\path\to\your-cli dir
```

It checks, per vector: that valid containers validate, that the canonical
manifest and the canonical renders match byte-for-byte, and that every invalid
container is rejected. Exit code is 0 when everything passes and 1 otherwise, so
either runner can gate a build. Both are registered with ctest, so `ctest` scores
the reference CLI on every platform, in both forms.

Scoring defaults to the TAR form, because that is the one an implementation may
not decline. If you support the directory form too, score it as well — but an
implementation that passes only `dir` is not conforming, since no one can hand
it a file.

### If you stop partway up the ladder

Most implementations will. Exit non-zero with **`E_UNIMPLEMENTED`** in the
output for a profile you did not evaluate, and the runner scores that vector
`N/A` and counts it separately:

```
  PASS  minimal
  N/A   signed - signed not implemented
  ...
passed 7, failed 0, not implemented 12
```

The count is always printed, because "0 failed" means something different when
a third of the vectors were never evaluated. What this buys you is a clean
score for the profiles you *do* claim, and nothing else — every other answer is
scored normally, so declining a profile is only ever a statement about
coverage. What it does not buy you is silence: reporting a profile as valid
without running its checks is the one behaviour `errors.md` rules out
completely.

Regenerating the packed vectors after editing a vector directory:

```sh
cmake -DMCDF_CLI=<cli> -DMODE=pack -P ../cmake/vectors.cmake
```

`ctest` runs the same script in `check` mode, so a vector edited without being
repacked fails the build rather than quietly scoring the stale archive.

## What is guaranteed byte-for-byte

Only four things depend on exact bytes; everything else is ordinary parsing:

1. **The canonical manifest** — RFC 8785 (JCS): object keys sorted by UTF-16
   code unit, no insignificant whitespace, minimal string escaping.
2. **The TAR packing** — normalized headers (mtime/uid/gid = 0, mode 0644, empty
   owner names), members ordered by path.
3. **`content.md` normalization** — LF line endings.
4. **The canonical render** (spec §10.4) — the HTML and plain text of the Render
   profile. Note what it does *not* contain: the name or version of the renderer
   that produced it. Output that identifies its producer cannot be reproduced by
   anyone else, which would make the profile unscoreable.

Signatures cover the *canonical* manifest, so reformatting `manifest.json`
whitespace does **not** break a signature; changing a hash does.
