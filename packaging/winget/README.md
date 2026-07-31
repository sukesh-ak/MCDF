<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# Windows Package Manager (winget)

Manifests for `winget install mcdf`. They live here because the first
submission is done by hand; after that the `winget` job in
[`release.yml`](../../.github/workflows/release.yml) updates the package on
each tag.

**Nothing is published yet.** The release job is gated on a `WINGET_TOKEN`
secret and does nothing until that exists — the same arrangement as the npm
job, and for the same reason: a package identifier is permanent, and every
registry name is one decision taken once.

## The package

| | |
|---|---|
| Identifier | `Sukeshak.MCDF` — permanent once accepted |
| Moniker | `mcdf`, so `winget install mcdf` resolves |
| Installer | the release's `mcdf-windows-x64.zip`, unchanged |
| Type | `zip` + `portable` |

Portable is the right type for a command-line tool. winget unpacks the archive
and puts a shim named `mcdf` in its Links directory, which is already on PATH,
so the command works in a new shell and `winget uninstall` removes it cleanly —
no setup program, no registry entries, nothing left behind. It works because
the CLI is built with the `x64-windows-static` triplet and a statically linked
MSVC runtime, so there are no loose DLLs and no redistributable to chase.

## Submitting the first version

1. Update `PackageVersion`, `InstallerUrl`, `InstallerSha256` and `ReleaseDate`
   in all three files. The hash is already published as the release's
   `SHA256SUMS` asset — no need to recompute it, though verifying is cheap:

   ```powershell
   (Get-FileHash mcdf-windows-x64.zip -Algorithm SHA256).Hash
   ```

2. Validate, then install from the local manifest to prove it actually works.
   Installing from a local manifest is disabled by default, so the first line
   needs an administrator shell and is only needed once per machine:

   ```powershell
   winget settings --enable LocalManifestFiles     # as administrator, once
   winget validate --manifest packaging\winget\manifests
   winget install  --manifest packaging\winget\manifests
   mcdf --version
   winget uninstall Sukeshak.MCDF
   ```

   Point `--manifest` at the `manifests` directory, not at `packaging\winget`:
   winget parses *every* file in the directory it is given, and chokes on this
   README.

3. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs), copy
   the three files to `manifests/s/Sukeshak/MCDF/<version>/`, and open a pull
   request. `Tools/SandboxTest.ps1` in that repository runs the install inside
   Windows Sandbox, which is what the reviewers check.

A new package gets human review, so expect the first one to take a few days.
Later versions are automated and usually merge within the hour.

## Worth knowing

**The executable is not code-signed.** winget does not require it, but
SmartScreen will warn on first run until it is. That is a certificate decision,
not a packaging one.

**x64 only.** Windows on ARM would need a second build in the release workflow
and a second entry under `Installers`.

**Versions are permanent.** A published version can be delisted but not
rewritten, so the manifest should point at a release that has already been
verified — which is why the job runs after the release job rather than beside
it.
