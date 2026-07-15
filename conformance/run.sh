#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
#
# Score an MCDF implementation against the conformance vectors.
#
#   ./run.sh                  # score `mcdf` from PATH (the reference runtime)
#   ./run.sh /path/to/my-cli  # score any CLI exposing the MCDF verbs
#
# The CLI under test must support:
#   <cli> validate <container> --profile <core|integrity>   exit 0 = valid
#   <cli> manifest <container>                              canonical JSON on stdout
#
# Dependency-free POSIX sh: no jq, no python.

CLI="${1:-mcdf}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PASS=0
FAIL=0

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

if ! command -v "$CLI" >/dev/null 2>&1 && [ ! -x "$CLI" ]; then
  echo "error: CLI not found: $CLI" >&2
  exit 2
fi

echo "MCDF conformance kit"
echo "implementation: $CLI"
echo

echo "validate (valid + invalid vectors)"
for d in "$HERE"/vectors/valid/*/ "$HERE"/vectors/invalid/*/; do
  [ -f "$d/case.json" ] || continue
  name=$(basename "$d")
  profile=$(field "$d/case.json" profile)
  expect=$(field "$d/case.json" expect)
  want_err=$(field "$d/case.json" error)
  [ -n "$profile" ] || profile=integrity

  out=$("$CLI" validate "$d/container" --profile "$profile" 2>&1)
  rc=$?

  if [ "$expect" = "pass" ]; then
    if [ $rc -eq 0 ]; then record "$name" 0; else record "$name" 1 "expected pass, got: $out"; fi
  else
    if [ $rc -eq 0 ]; then
      record "$name" 1 "expected rejection ($want_err)"
    elif [ -n "$want_err" ] && ! printf '%s' "$out" | grep -q "$want_err"; then
      record "$name" 1 "rejected, but not with $want_err"
    else
      record "$name" 0
    fi
  fi
done

echo
echo "canonical manifest (byte-for-byte)"
for d in "$HERE"/vectors/canonical/*/; do
  [ -f "$d/case.json" ] || continue
  name=$(basename "$d")
  if "$CLI" manifest "$d/container" 2>/dev/null | diff -q - "$d/expected/manifest.json" >/dev/null 2>&1; then
    record "$name" 0
  else
    record "$name" 1 "output differs from expected/manifest.json"
  fi
done

echo
echo "-------------------------------------"
printf 'passed %d, failed %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
