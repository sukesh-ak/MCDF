# MCDF Licensing

MCDF is **dual-licensed by content type**. This is deliberate: a document format
has two separable things to license — the written **specification** and the
**code** that implements it — and each is best served by a different,
royalty-free (RF) license.

## What governs what

| Content | License | SPDX identifier | Full text |
|---|---|---|---|
| Specification text — `spec/` (the RFC and all spec documents) | Community Specification License 1.0 | `Community-Spec-1.0` | [`spec/LICENSE`](spec/LICENSE) |
| Vendored third-party code — `micro/render/md4c/` | MIT | `MIT` | [`micro/render/md4c/LICENSE.md`](micro/render/md4c/LICENSE.md) |
| Source code & tooling — everything else | Apache License 2.0 | `Apache-2.0` | [`LICENSE`](LICENSE) |

> Private, unpublished working notes are **excluded from version control** (see
> [`.gitignore`](.gitignore)). They are not licensed for distribution and are
> not part of the specification.

**Vendored code keeps its own licence and its own copyright.** `mcdf_micro` is
published as an ESP-IDF component, where there is no package manager to resolve
a dependency from, so its one dependency is carried in-tree instead. It is an
unmodified copy, its licence travels with it, and
[`micro/render/md4c/README.md`](micro/render/md4c/README.md) records the version
and the upstream file hashes — which is also what proves it is unmodified. Every
other third-party component is resolved by vcpkg at build time and is not
redistributed here; see the dependency list in [`README.md`](README.md).

## Why this split

- **Apache-2.0 for code.** Its Section 3 gives an explicit, per-contributor,
  perpetual, worldwide, royalty-free **patent grant** with **defensive
  termination** — exactly the RF patent posture MCDF wants for implementations.
- **CSL-1.0 for the specification.** Apache-2.0 is written for a "Work" (software);
  a prose specification is better served by a purpose-built spec license. CSL
  grants everyone the right to build **independent, interoperable implementations**,
  binds Contributors to a royalty-free **patent commitment**, and provides a formal
  **patent-exclusion process** (recorded in [`spec/NOTICE.md`](spec/NOTICE.md)).

## Marking files (SPDX)

Every new file SHOULD declare its license with an SPDX identifier.

Code (Apache-2.0):

```
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
```

Specification documents (CSL-1.0) — Markdown, as an HTML comment at the top of the file:

```
<!-- SPDX-License-Identifier: Community-Spec-1.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->
```

## Contributions

- Contributions to **code** are inbound = outbound under **Apache-2.0** (Apache-2.0 §5),
  unless a contributor explicitly states otherwise.
- Contributions to the **specification** are under **CSL-1.0**. The CSL bundle
  includes a Contributor License Agreement and Working-Group governance;
  contributors accept CSL per its §2.1.3.3 by recording acceptance in
  [`spec/NOTICE.md`](spec/NOTICE.md).

## Before first public release

- [x] Copyright holder set to **The MCDF Project** (maintained by Sukesh Ashok
  Kumar) across all notices.
- [x] Code-of-Conduct contact recorded in [`spec/NOTICE.md`](spec/NOTICE.md).
- [ ] Add a second Code-of-Conduct contact once a collaborator joins (CSL
  recommends two named individuals).
- [ ] Adopt the remaining CSL Working-Group documents (scope, governance,
  contribution guide) if/when the project takes outside contributions — from the
  canonical bundle: <https://github.com/CommunitySpecification/1.0>.
- [ ] Confirm `Community-Spec-1.0` and `Apache-2.0` resolve correctly in whatever
  SBOM / license-scanning tooling the project adopts.

---

> This file documents the project's licensing choices for contributors and
> implementers. It is **not legal advice**; if MCDF is taken to a formal standards
> body, have counsel confirm the license instruments and Working-Group structure.
