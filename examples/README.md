<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 The MCDF Project -->

# MCDF examples

**`showcase.mcdf`** is a complete, signed MCDF document that demonstrates every
feature of the format — structure binding, integrity manifest, a detached
signature, a hash-chained audit log with a signed checkpoint, and an embedded
image. **`showcase/`** is the same document in its unpacked (directory) form so
you can read every member right here on GitHub: it is just Markdown, YAML, and
JSON.

## View it

- **MCDF Studio:** File → *Open .mcdf document…* → `showcase.mcdf`. The
  Structure, Manifest, Trust, Audit, Conformance, and Container panels all
  light up (View menu). The status bar shows a green **signed** shield.
- **CLI:**

  ```sh
  mcdf inspect  examples/showcase.mcdf
  mcdf verify   examples/showcase.mcdf
  mcdf validate examples/showcase.mcdf --profile render   # the full ladder
  mcdf render html examples/showcase.mcdf -o showcase.html
  ```

## The two-minute tamper demo (Studio)

1. Open `showcase.mcdf` — trust shield **green**, every manifest dot green.
2. Type one character in the editor — that file's dot turns **amber** and the
   signature flips **red**: it no longer covers what you see.
3. Manifest panel → **Rebuild** — dots go green, signature stays red (the
   canonical manifest changed underneath it).
4. Trust panel → **Generate** a key (one click), then **Sign** — everything
   green again, now signed by *your* `did:key`.

That is the format's whole pitch, watched live: tamper-evidence as a state,
not a report. (The original signing key is intentionally not distributed —
editing this document is *supposed* to break its signature.)

## Try the confidential layer

```sh
mcdf keygen --out my-x25519.pem --type x25519      # prints your did:key
mcdf unpack examples/showcase.mcdf -o my-copy
mcdf encrypt my-copy --recipient did:key:z6LS...    # your DID from above
mcdf decrypt my-copy --key my-x25519.pem
```

Studio's Encryption panel does the same with clicks (and locks the editor
while content is ciphertext).
