<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project -->

# Accessibility support statement — MCDF Web

**Target: WCAG 2.2 Level AA.** This is not a nice-to-have bolted onto a document
editor; it is why this client exists at all. MCDF Studio, the native flagship,
is built on ImGui, which draws its own widgets and exposes nothing to a screen
reader. "Documents for everyone" has to include the people ImGui cannot serve,
so accessible authoring lives here — on the platform where accessibility is
native.

Status: **partial conformance, actively developed.** The claims below describe
what is implemented and tested today. Anything not listed is
not yet claimed.

Last automated verification: **2026-07-30** — 0 axe violations across the
default, editor-only and preview-only layouts, **and with every panel expanded**
(Trust, Encryption, Audit, Diff), in both colour schemes; landmarks, skip link,
keyboard reachability, the tamper announcement and the offline reload all
confirmed in Chromium (26/26 Playwright tests).

## What the toolchain enforces

- **Svelte compiler a11y warnings are build failures.** `npm run check` runs
  `svelte-check --fail-on-warnings`, so a missing label, an invalid ARIA
  attribute, or a click handler on a non-interactive element cannot reach a
  build. Choosing a compiler that checks this was the deciding argument for
  Svelte over React.
- **axe-core runs against the production build in CI** across the default,
  editor-only and preview-only layouts and every panel expanded, in both light
  and dark schemes. This is not ceremony: it is what caught the CodeMirror merge
  view shipping two unnamed `role="textbox"` surfaces (WCAG 4.1.2), which is now
  fixed by naming each side of the diff.
- Automated tools catch roughly a third of WCAG failures. They gate the build;
  they do not substitute for the manual pass below.

## Implemented

**Structure and navigation**
- Real landmarks: `banner`, `main`, `complementary` (labelled "Document
  panels"), `contentinfo`. Panels are `<section>`s with headings, not `<div>`s.
- A skip link is the first focusable element on the page.
- The document outline is a `<nav>` with an ordered list; heading levels are
  announced along with the text.

**Keyboard**
- Every action has a keyboard path; there are no pointer-only affordances.
- <kbd>Ctrl</kbd>/<kbd>Cmd</kbd>+<kbd>S</kbd> saves.
- Panels are native `<details>`/`<summary>` disclosures, so expand/collapse
  state is operable and announced without custom ARIA.
- The layout switcher is a real radio group: arrow keys move between options.
- CodeMirror 6 is the editor precisely because it keeps caret navigation,
  selection and screen-reader interaction working in a way a styled `<div>`
  cannot.

**Status and feedback**
- Two permanently-present live regions: `aria-live="polite"` for routine status
  and `role="alert"` for integrity failures. They exist in the DOM from first
  paint, because a region inserted at the moment it gains text is announced
  unreliably.
- **The tamper demo is audible.** Editing a document whose manifest no longer
  matches announces "Manifest no longer matches the document. Any signature over
  it is now invalid." — it is not conveyed by colour alone.
- **A signature breaking is announced too**, one level up: "Signature no longer
  verifies: this document has changed since it was signed", and "Signatures
  verify again" when re-signing repairs it. This is the most consequential thing
  the app can say, so it goes to `role="alert"`.
- Signature verdicts, the audit chain state and checkpoint validity each sit in
  their own live region, so a change reaches assistive technology without the
  user hunting for a recoloured badge.
- Validation results are shown with their normative error codes
  (`E_MANIFEST_HASH_MISMATCH` and friends) in a live region.
- Every signature and audit row carries a text label and a symbol alongside its
  colour; nothing is conveyed by colour alone.

**Encrypted documents**
- When content is sealed, the editor is replaced by a short explanation naming
  the panel that can unseal it — not by a disabled or empty text box, which
  would leave a screen-reader user to work out why typing does nothing.
- The diff's two panes are named ("content.md as last saved" / "with the current
  edits"), so a screen reader can tell which side it is in.

**Presentation**
- Light and dark schemes are both authored explicitly; text pairs meet AA
  contrast (4.5:1 body, 3:1 for large text and UI borders). The system
  preference is the default and an explicit choice is remembered.
- `prefers-reduced-motion` is respected; `prefers-contrast: more` strengthens
  borders.
- Manifest status uses a shape, a text label **and** colour — never colour
  alone.
- Interactive targets are at least 24×24 CSS px (SC 2.5.8).
- A single consistent focus indicator (2 px outline, 2 px offset) on every
  control, including inside CodeMirror.
- The layout reflows to a single column below 60rem without horizontal
  scrolling; wide content (tables, code) scrolls within its own container.

## Known gaps

| Gap | Notes |
|---|---|
| **No manual NVDA / VoiceOver / Orca pass recorded yet** | The largest remaining gap, and the one automation cannot close. Every claim above is verified by axe and by Playwright assertions on announced text; none of it is verified by a person listening. |
| No visible keyboard-shortcut reference in the UI | <kbd>Ctrl</kbd>/<kbd>Cmd</kbd>+<kbd>S</kbd> is undiscoverable without documentation. |
| Preview does not expose heading anchors as skip targets | Long documents are navigable in the outline but not within the preview itself. |
| The CodeMirror merge view is third-party DOM | Its panes are named and it is axe-clean, but its internal keyboard model is CodeMirror's, not ours. The member-change list above it conveys the same information in plain HTML, so nothing is diff-view-only. |
| Colour-scheme choice is remembered, text size is not | The app inherits browser zoom and font size; it does not offer its own control. |

Closed since the last revision: the outline's "jump to heading" now moves the
caret (and queues the jump if the editor is still loading), and the Trust,
Encryption, Audit and Diff panels exist and are covered by the axe sweep.

## Testing

```sh
cd apps/web
docker compose run --rm test                      # includes svelte-check
npm run test:e2e --workspace packages/app         # Playwright + axe
```

Each milestone additionally requires a manual screen-reader pass; results will
be recorded here as they are done.

## Feedback

Accessibility problems are bugs, and they are not lower priority than any other
bug. Please report them at
<https://github.com/sukesh-ak/MCDF/issues> with your browser, assistive
technology and version.
