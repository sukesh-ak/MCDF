<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# mcdf-ts

An independent TypeScript implementation of the **Markdown Container Document
Format** — documents that carry their own proof.

No DOM dependencies: the same build runs in Node, in a browser and in a worker.
It is written from the [specification](https://github.com/sukesh-ak/MCDF/tree/main/spec)
and the [conformance kit](https://github.com/sukesh-ak/MCDF/tree/main/conformance)
alone — not bound to, generated from, or compiled out of the reference C++
engine. That independence is the point: it turns "re-implementable in any
language" into something a test suite can check.

```sh
npm install mcdf-ts
```

> **Not on npm yet** — publishing waits for MCDF v1.0. Until then, build it from
> the repository: from `apps/web`, run `docker compose run --rm install` and
> `npm run build --workspace packages/mcdf-ts`. Everything below already works;
> only the install line is waiting.

## What an MCDF document is

A directory (or a USTAR archive named `.mcdf`) of ordinary files:

```
example.mcdf/
├── content.md        CommonMark; headings carry {#id} anchors
├── schema.yaml       document_type + sections[], each bound to a heading
├── metadata.yaml     title, version, authors, classification, language
├── manifest.json     path -> sha256, the root of trust        (Integrity+)
├── signatures/*.sig  detached JWS over the canonical manifest (Signed+)
├── encryption/       policy.yaml: AES-256-GCM + HPKE-wrapped  (Encrypted+)
├── audit.log         hash-chained history; audit.checkpoint signs its head
└── assets/…
```

**Reading, writing and modifying a document needs no cryptography.** Everything
above the Core profile is optional.

## Usage

```ts
import {
  MemoryContainer,
  loadDocument,
  validate,
  buildManifest,
  manifestToCanonicalJson,
} from 'mcdf-ts';

// Open a .mcdf archive (Uint8Array from fetch, a file input, or fs).
const container = MemoryContainer.fromTar(bytes);
const doc = loadDocument(container);

console.log(doc.metadata.title, doc.headings);

const report = validate(container, doc, 'integrity');
if (!report.ok) {
  for (const issue of report.issues) {
    console.error(issue.code, issue.message); // e.g. E_MANIFEST_HASH_MISMATCH
  }
}

// Edit, then rebuild the manifest and repack. Packing is deterministic: the
// same document always produces the same bytes.
container.writeText('content.md', '# Overview {#overview}\n\nNew text.\n');
const manifest = buildManifest(container);
container.writeText('manifest.json', `${manifestToCanonicalJson(manifest)}\n`);
const packed = container.toTar();
```

### Signing

```ts
import { SigningKey, signContainer, signaturePath, verifyContainer } from 'mcdf-ts';

const key = SigningKey.generate('EdDSA'); // or 'ES256'
container.writeText(signaturePath('author'), signContainer(container, key));

verifyContainer(container);
// [{ file: 'signatures/author.sig', valid: true, alg: 'EdDSA', kid: 'did:key:z6Mk…' }]
```

A signature is a detached JWS over the **canonical manifest**, so reformatting
`manifest.json` cannot break it and changing any member's bytes always does.
Signing is synchronous, which keeps `validate()` synchronous too.

### Encryption and audit

```ts
import {
  EncPrivateKey,
  encryptContainer,
  decryptContainer,
  auditAppend,
  auditVerify,
  auditCheckpoint,
} from 'mcdf-ts';

const recipient = EncPrivateKey.generate(); // X25519
await encryptContainer(container, ['content.md'], [recipient.publicKey()]);
await decryptContainer(container, recipient);

auditAppend(container, 'REVIEWED', 'Sam', new Date().toISOString());
auditVerify(container); // { ok: true, entries: 1 }
auditCheckpoint(container, key); // signs the log head
```

AES-256-GCM comes from WebCrypto and HPKE from `@hpke/core`, so both are
asynchronous; everything else in the library is not.

### CLI

The package ships `mcdf-ts`, mirroring the reference `mcdf` CLI's verbs and
flags so the two are interchangeable in scripts:

```sh
npx mcdf-ts validate ./example.mcdf --profile signed
npx mcdf-ts manifest ./example.mcdf          # canonical JSON on stdout
npx mcdf-ts inspect  ./example.mcdf
npx mcdf-ts pack     ./example/ out.mcdf
npx mcdf-ts unpack   ./out.mcdf ./example/

npx mcdf-ts keygen   --out author.pem        # prints the did:key
npx mcdf-ts sign     ./example --key author.pem
npx mcdf-ts verify   ./example

npx mcdf-ts keygen   --out recipient.pem --type x25519
npx mcdf-ts encrypt  ./example --recipient did:key:z6LS…
npx mcdf-ts decrypt  ./example --key recipient.pem

npx mcdf-ts audit    ./example --append REVIEWED --actor Sam
npx mcdf-ts audit    ./example --checkpoint --key author.pem
```

Keys are unencrypted PKCS#8 PEM — byte-compatible with what the C++
`mcdf keygen` writes, so one key file works across both implementations and the
browser client.

## Conformance

**Every published vector passes**, and canonical manifest output is
byte-identical to the reference C++ CLI. CI scores both implementations with the
same `conformance/run.sh` on every push.

Profiles implemented: **Core**, **Integrity**, **Signed**, **Encrypted**. The
**Render** profile reports `E_UNIMPLEMENTED` rather than silently passing — a
canonical render must match the reference `md4c` output byte for byte, and there
are no published render vectors to prove that against yet. A validator that
returns "valid" for checks it never ran is worse than one that admits the gap.

## Determinism

Byte-parity with the reference implementation is a hard requirement, not a
nice-to-have: the same document must hash identically whichever implementation
packed it. Three things are therefore implemented in-house rather than taken
from a package, and pinned by tests:

- **USTAR writing** — members ordered by path, `mtime`/`uid`/`gid` zeroed, mode
  `0644`, empty owner names, two trailing zero blocks.
- **RFC 8785 (JCS)** canonical JSON — object keys sorted by UTF-16 code unit,
  no insignificant whitespace, minimal escaping.
- **The YAML writer** — no general-purpose emitter guarantees a byte layout, and
  the manifest hashes what is on disk.

TAR member paths sort by **UTF-8 bytes**, which is not JavaScript's default `<`
comparison; JCS object keys sort by **UTF-16 code units**, which is. Both are
handled explicitly.

## API

| Area | Exports |
|---|---|
| Containers | `MemoryContainer`, `Container`, `checkMemberPath` |
| Archives | `tarWrite`, `tarRead` |
| Documents | `loadDocument`, `saveDocument`, `createDocument` |
| Manifests | `buildManifest`, `verifyManifest`, `manifestToCanonicalJson`, `parseManifestJson`, `isManifestExcluded` |
| Validation | `validate`, `PROFILES`, `isProfile` |
| Serialization | `canonicalizeValue`, `canonicalizeJson`, `parseMetadataYaml`, `parseSchemaYaml`, `metadataToYaml`, `schemaToYaml`, `canonicalizeContent` |
| Markdown | `parseHeadings`, `renderHtml`, `splitHeadingId` |
| Hashing | `sha256Hex`, `hashHex` |
| Keys | `SigningKey`, `VerifyKey`, `EncPrivateKey`, `EncPublicKey`, `encodeDidKey`, `decodeDidKey` |
| Signing | `jwsSignDetached`, `jwsVerifyDetached`, `signContainer`, `verifyContainer`, `canonicalManifestBytes`, `signaturePath` |
| Encryption | `encryptContainer`, `decryptContainer`, `readEncryptionPolicy`, `encryptedMembers`, `aes256gcmSeal`/`Open`, `hpkeSeal` |
| Audit | `readAuditLog`, `auditAppend`, `auditVerify`, `auditCheckpoint`, `auditVerifyCheckpoint` |
| Encoding | `base64urlEncode`/`Decode`, `base58btcEncode`/`Decode` |

Node-only filesystem helpers live behind the `mcdf-ts/node` subpath, kept out of
the main entry point so browser bundles never pull in `node:fs`:

```ts
import { openContainer, DirectoryContainer } from 'mcdf-ts/node';
```

## Security notes

- The signature allow-list is **EdDSA (Ed25519)** and **ES256 (ECDSA P-256)**,
  and a JWS header may not claim an algorithm its `did:key` cannot speak — the
  algorithm-confusion guard is tested, not assumed.
- Signing and verification use [`@noble/curves`][noble] rather than WebCrypto:
  Ed25519 support in WebCrypto is still uneven across browsers, and a signature
  check that works in one browser and not another is not a check.
- AES-256-GCM uses WebCrypto, which is native everywhere this runs. The member's
  path is the AAD, so a ciphertext moved to another member fails to
  authenticate rather than decrypting in the wrong place.
- Verification never throws on malformed input: every failure is a structured
  verdict carrying its normative error code.

## License

Apache-2.0. Runtime dependencies: `@noble/hashes`, `@noble/curves`,
`@hpke/core`, `js-yaml`, `micromark` — all MIT.

[noble]: https://github.com/paulmillr/noble-curves
