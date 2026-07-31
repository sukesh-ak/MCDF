<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# MCDF Web

The accessible browser client for MCDF, and `mcdf-ts` — an **independent
TypeScript implementation** of the format underneath it.

Two things live here, and they are worth keeping apart:

| Package | What it is | Depends on the DOM? |
|---|---|---|
| [`packages/mcdf-ts`](packages/mcdf-ts) | The MCDF core: containers, deterministic USTAR, JCS, manifests, signatures, encryption, audit chains, validation. An npm library and a Node CLI. | No — Node, browsers and workers alike |
| [`packages/app`](packages/app) | The SPA: Svelte 5 + TypeScript, CodeMirror 6, WCAG 2.2 AA | Yes — it owns *all* the DOM |

`mcdf-ts` is written from the [specification](../../spec) and the
[conformance kit](../../conformance) only — no bindings to the C++ engine and no
WASM build of it. That is the point: it makes "re-implementable in any language"
a checkable claim rather than a promise, and every gap it finds in the spec is a
spec bug to fix.

## Why this client exists

MCDF Studio is the power-user flagship, but ImGui draws its own widgets and has
no screen-reader support. Accessible authoring has to live where accessibility
is native, so it lives here: real DOM, semantic HTML, keyboard-first, and status
changes announced rather than merely recoloured. See
[ACCESSIBILITY.md](ACCESSIBILITY.md).

Nothing is uploaded. Documents are parsed, hashed and verified in the tab, and
the app works offline.

## Running it

Everything runs in containers — no local Node install needed.

```sh
cd apps/web
docker compose run --rm install     # once, populates the dependency volumes
docker compose up dev               # http://localhost:5173/MCDF/  (hot reload)
docker compose run --rm test        # unit tests + checks + conformance + licenses
docker compose run --rm e2e         # Playwright + axe against the production build
docker compose up preview           # http://localhost:8080/MCDF/  (production build)
```

> Two things in the compose file exist because of bind mounts on Windows/macOS,
> and both look like over-engineering until you hit them:
>
> - **`node_modules` lives in Linux-native volumes**, not on the mount. That is
>   the difference between a dev server ready in ~0.5 s and one that takes over
>   two minutes — Vite's dependency pre-bundling makes tens of thousands of small
>   file reads, and every one crossing the VM boundary is what stalls it.
> - **The dev service sets `MCDF_POLL=1`.** Bind mounts deliver no inotify events
>   into the container, so Vite's watcher goes deaf and hot reload quietly serves
>   stale modules with no error at all. Polling is the only fix.

There is also a self-contained image, which is what CI effectively runs:

```sh
docker build -f apps/web/Dockerfile --target test .   # from the repo root
docker build -f apps/web/Dockerfile -t mcdf-web . && docker run --rm -p 8080:8080 mcdf-web
```

With Node ≥ 20 installed locally, the plain npm scripts work too:

```sh
npm install
npm run build          # mcdf-ts, then the app
npm test               # vitest across both packages
npm run check          # tsc + svelte-check (--fail-on-warnings: a11y warnings fail)
npm run conformance    # score the mcdf-ts CLI with conformance/run.sh
npm run license-audit  # MIT/Apache-2.0 gate
npm run dev            # http://localhost:5173/MCDF/
```

## Conformance status

`mcdf-ts` is scored by the same `conformance/run.sh` as the reference CLI, and
**passes every published vector**:

```
validate (valid + invalid vectors)
  PASS  encrypted     PASS  minimal        PASS  signed-es256  PASS  signed
  PASS  bad-algo      PASS  extra-file     PASS  hash-mismatch
  PASS  missing-content              PASS  policy-unsound
  PASS  required-section-missing     PASS  sig-alg-not-allowed
  PASS  sig-invalid                  PASS  sig-missing
canonical manifest (byte-for-byte)
  PASS  manifest
-------------------------------------
passed 14, failed 0
```

The signatures in those vectors were produced by the C++ implementation and are
verified here with no shared code — which is the whole claim, made checkable.

**Render** is the one profile `mcdf-ts` does not claim: a canonical render has
to be byte-identical to the reference `md4c` output and there are no published
render vectors yet, so it reports `E_UNIMPLEMENTED` rather than passing on the
strength of the live preview. See
[`conformance/errors.md`](../../conformance/errors.md) for what that code means
and why it is deliberately not normative.

## What is built

Everything below is shipped and covered by CI:

- Workspace on Vite + Svelte 5 + TypeScript, with a license gate.
- `mcdf-ts` at Core and Integrity, plus a Node CLI the conformance kit scores.
- The SPA shell: open and save, CodeMirror 6 with live preview, metadata
  editing, document outline.
- A manifest panel with live per-file digests and the container hash.
- Ed25519 / ES256 JWS signing, `did:key`, an IndexedDB keystore, and the
  re-sign demo.
- HPKE + AES-GCM encryption, and the hash-chained audit log with checkpoints.
- Diff view, PWA and offline support, Pages deployment, a WCAG audit.

## Trust, confidentiality and history

Three panels, and the one thing each is really for:

- **Trust** — a keyring (IndexedDB), signing with Ed25519 or ES256, and the
  demo the format exists for: edit a signed document and the signature goes red
  *while you type*, then re-sign and it comes back. The verdict is announced to
  a screen reader, not merely recoloured.
- **Encryption** — AES-256-GCM over chosen members with the content key wrapped
  to each recipient by HPKE. While sealed the editor is replaced by an
  explanation rather than an empty box: writing a text buffer over ciphertext
  would destroy it.
- **Audit** — the hash-chained log, and a signed checkpoint over its head. The
  chain catches edits and reordering; the checkpoint catches the tail being cut
  off, which a chain alone cannot.

Keys never leave the browser. They are stored as raw private bytes in
IndexedDB — origin-scoped, but readable by anything running on this origin, and
the UI says so. For a key that matters, keep the PKCS#8 PEM offline and import
it when needed; it is the same file `mcdf keygen` and `mcdf-ts keygen` write, so
one key works across all three clients.

## Offline

The app is an installable PWA and works with the network switched off — proved
by an end-to-end test that actually cuts it, rather than by assertion. The
service worker is generated at build time by
[`packages/app/vite-pwa.ts`](packages/app/vite-pwa.ts) (about sixty lines, no
Workbox) and precaches exactly what the build emitted.

A new version never activates under an open editor. It waits, the app offers a
reload, and the user picks the moment — swapping the running app out from under
an unsaved document is data loss wearing a feature's clothes.

## Layering

The app owns view state; every format and integrity operation is delegated to
`mcdf-ts`. No component parses YAML, frames a TAR header or computes a digest —
the same law Studio follows with `studio_core`. If you find yourself reaching for
a parser inside a `.svelte` file, the function belongs in `mcdf-ts` instead.

## Dependencies

MIT/Apache-2.0 only, enforced by `scripts/license-audit.mjs` in CI. Anything that
reaches the browser bundle is audited strictly; dev/CI-only packages get a wider
list plus explicit, documented exceptions (today: `axe-core`, MPL-2.0, which
never ships).

In-house rather than dependencies: deterministic USTAR, RFC 8785 canonical JSON,
content canonicalization, the YAML *writer* (byte-parity with the C++ emitter is
required, and no general-purpose emitter guarantees a byte layout), base64url
and base58btc, `did:key`, detached JWS, the PKCS#8 reader/writer, and the
service worker.

Crypto primitives are **not** in-house: `@noble/curves` for Ed25519 and P-256,
`@hpke/core` for RFC 9180, and WebCrypto for AES-256-GCM and randomness.

## Deploying

`.github/workflows/pages.yml` publishes to GitHub Pages on a `v*` tag or on
demand. Pages serves a project site from `/<repo>/`, so the build uses
`base: '/MCDF/'`; override with `MCDF_BASE=/` for a custom domain. HTTPS there is
automatic and non-negotiable — WebCrypto refuses to work outside a secure
context, so signing would fail silently on plain HTTP.
