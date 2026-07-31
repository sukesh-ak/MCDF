<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# Vendored: md4c

**This directory is third-party code under its own licence.** See
[LICENSE.md](LICENSE.md) — MIT, Copyright © 2016-2024 Martin Mitáš. Nothing
here is MCDF's, and nothing here has been modified.

| | |
|---|---|
| Upstream | https://github.com/mity/md4c |
| Version | `release-0.5.3` |
| Vendored | 2026-07-31 |
| Files | `md4c.c`, `md4c.h`, `LICENSE.md` |

```
f12907817a17ae7d0f6c8d18770df839f187cad5649dd36a475dba0675c5c1f8  md4c.c
4efd19bf7ec270691d5b4189f496886e421768a814b5e817eb945aa85e859f18  md4c.h
d30937367d5413e7eaa218b1640b8946ff76fd34d97152f6979fd96169d5d0fc  LICENSE.md
```

## Why a copy and not a package

The rest of the repo takes md4c from vcpkg, which is right for a desktop build
and impossible for a microcontroller one: there is no vcpkg under ESP-IDF, and
a component that needed one would not be installable from the registry it
publishes to. The parser is two self-contained files with no dependency beyond
libc, so carrying them costs a directory and buys a build that works on a
toolchain that has no package manager at all.

The licence is what makes that free of consequences — MIT permits it outright,
and md4c was already an approved dependency of this project. Keeping the copy
here rather than at the repo root keeps it out of the C++ engine's build, which
continues to resolve md4c through vcpkg. The two never meet.

`md4c-html.c` and `entity.c` are deliberately absent. They are md4c's HTML
*renderer*; this library emits a block/span event stream and lets a layout
engine decide what to draw, so it needs the parser and nothing else. `md4c.c`
includes only `md4c.h` and libc, which is why "two files" is the whole of it.

## Upgrading

Replace the three files wholesale, update the version and the hashes above, and
run the suite. **Do not patch them in place.** A local modification is invisible
at the call site and turns every future upgrade into a merge; anything MCDF
needs to change belongs in `../render.c`, on our side of the seam. If upstream
ever needs a fix, send it upstream.

The hashes are the check that this stayed true: they are the upstream release's
own bytes, and any edit here breaks them.
