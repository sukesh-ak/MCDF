#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Score an MCDF implementation against the conformance vectors.
#
#   ./run.sh                      # score `mcdf` from PATH (the reference runtime)
#   ./run.sh /path/to/my-cli      # score any CLI exposing the MCDF verbs
#   ./run.sh /path/to/my-cli dir  # also score the OPTIONAL directory form
#
# The CLI under test must support:
#   <cli> validate <container> --profile <core|integrity|signed|encrypted|render>
#                                                           exit 0 = valid
#   <cli> manifest <container>                              canonical JSON on stdout
#   <cli> render <html|text> <container>                    canonical render on stdout
#
# Every vector is published in both serializations: `container/` to read and
# diff, `container.mcdf` to hand over. Scoring defaults to the TAR form because
# spec §3 makes it the one REQUIRED serialization - it is what any two
# implementations are guaranteed to share, and an implementation with no
# filesystem can be scored on nothing else. Pass `dir` to score the OPTIONAL
# directory form instead, which only implementations that support it will pass.
#
# An implementation may stop partway up the profile ladder. One that says so -
# by failing with E_UNIMPLEMENTED, which errors.md reserves for exactly this -
# is scored "not implemented" for that vector rather than passed or failed, and
# the count is printed. Anything else it reports is scored normally, so
# declining a profile buys nothing but honesty about which profiles it claims.
#
# Dependency-free POSIX sh: no jq, no python.

CLI="${1:-mcdf}"
FORM="${2:-tar}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PASS=0
FAIL=0
SKIP=0

case "$FORM" in
  tar|dir) ;;
  *) echo "error: form must be 'tar' or 'dir', got '$FORM'" >&2; exit 2 ;;
esac

# Byte-exact checks cannot go through `$(...)`, which strips trailing newlines,
# so the CLI's streams land in files.
WORK="${TMPDIR:-/tmp}/mcdf-conformance-$$"
mkdir -p "$WORK" || { echo "error: cannot create $WORK" >&2; exit 2; }
trap 'rm -rf "$WORK"' EXIT INT TERM
OUT="$WORK/out"
ERR="$WORK/err"

# Runs the CLI, capturing stdout and stderr separately. Sets RC.
run_cli() {
  "$@" >"$OUT" 2>"$ERR"
  RC=$?
}

# True when the run declined the profile rather than judging the document.
declined() {
  [ "$RC" -ne 0 ] && grep -q 'E_UNIMPLEMENTED' "$OUT" "$ERR" 2>/dev/null
}

# The container path for a vector, in the selected serialization.
container_for() {
  if [ "$FORM" = tar ]; then printf '%s' "$1/container.mcdf"
  else printf '%s' "$1/container"
  fi
}

# Extract a flat string field from a small JSON file without jq.
field() {
  grep -o "\"$2\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" "$1" 2>/dev/null |
    sed 's/.*"\([^"]*\)"$/\1/'
}

record() { # name, ok(0/1), detail
  if [ "$2" -eq 0 ]; then
    printf '  PASS  %s\n' "$1"
    PASS=$((PASS + 1))
  else
    printf '  FAIL  %s%s\n' "$1" "${3:+ - $3}"
    FAIL=$((FAIL + 1))
  fi
}

skip() { # name, detail
  printf '  N/A   %s%s\n' "$1" "${2:+ - $2}"
  SKIP=$((SKIP + 1))
}

if ! command -v "$CLI" >/dev/null 2>&1 && [ ! -x "$CLI" ]; then
  echo "error: CLI not found: $CLI" >&2
  exit 2
fi

echo "MCDF conformance kit"
echo "implementation: $CLI"
if [ "$FORM" = tar ]; then
  echo "serialization:  tar (REQUIRED, spec 3)"
else
  echo "serialization:  directory (OPTIONAL)"
fi
echo

echo "validate (valid + invalid vectors)"
for d in "$HERE"/vectors/valid/*/ "$HERE"/vectors/invalid/*/; do
  [ -f "$d/case.json" ] || continue
  name=$(basename "$d")
  profile=$(field "$d/case.json" profile)
  expect=$(field "$d/case.json" expect)
  want_err=$(field "$d/case.json" error)
  [ -n "$profile" ] || profile=integrity
  c=$(container_for "$d")

  if [ ! -e "$c" ]; then
    record "$name" 1 "missing $FORM form - repack with cmake -DMODE=pack -P cmake/vectors.cmake"
    continue
  fi

  run_cli "$CLI" validate "$c" --profile "$profile"
  if declined; then
    skip "$name" "$profile not implemented"
    continue
  fi
  out=$(cat "$OUT" "$ERR")

  if [ "$expect" = "pass" ]; then
    if [ "$RC" -eq 0 ]; then record "$name" 0; else record "$name" 1 "expected pass, got: $out"; fi
  else
    if [ "$RC" -eq 0 ]; then
      record "$name" 1 "expected rejection ($want_err)"
    elif [ -n "$want_err" ] && ! printf '%s' "$out" | grep -q "$want_err"; then
      record "$name" 1 "rejected, but not with $want_err"
    else
      record "$name" 0
    fi
  fi
done

echo
echo "canonical bytes (byte-for-byte)"
for d in "$HERE"/vectors/canonical/*/; do
  [ -f "$d/case.json" ] || continue
  name=$(basename "$d")
  check=$(field "$d/case.json" check)
  [ -n "$check" ] || check=canonical-manifest
  c=$(container_for "$d")

  if [ ! -e "$c" ]; then
    record "$name" 1 "missing $FORM form - repack with cmake -DMODE=pack -P cmake/vectors.cmake"
    continue
  fi

  case "$check" in
    canonical-manifest)
      run_cli "$CLI" manifest "$c"
      if declined; then
        skip "$name" "canonical manifest not implemented"
      elif diff -q "$OUT" "$d/expected/manifest.json" >/dev/null 2>&1; then
        record "$name" 0
      else
        record "$name" 1 "output differs from expected/manifest.json"
      fi
      ;;
    canonical-render)
      # Both renderings are scored, and separately: an implementation that has
      # the HTML exactly right and the plain text wrong has one bug, not none.
      for fmt in html text; do
        [ "$fmt" = html ] && want="$d/expected/render.html" || want="$d/expected/render.txt"
        run_cli "$CLI" render "$fmt" "$c"
        if declined; then
          skip "$name ($fmt)" "canonical render not implemented"
        elif diff -q "$OUT" "$want" >/dev/null 2>&1; then
          record "$name ($fmt)" 0
        else
          record "$name ($fmt)" 1 "output differs from $(basename "$want")"
        fi
      done
      ;;
    *)
      record "$name" 1 "unknown check: $check"
      ;;
  esac
done

echo
echo "-------------------------------------"
if [ "$SKIP" -eq 0 ]; then
  printf 'passed %d, failed %d\n' "$PASS" "$FAIL"
else
  # Printed, never hidden: a score of "0 failed" means something different when
  # a third of the vectors were never evaluated, and the reader of this line is
  # entitled to know which.
  printf 'passed %d, failed %d, not implemented %d\n' "$PASS" "$FAIL" "$SKIP"
fi
[ "$FAIL" -eq 0 ] || exit 1
