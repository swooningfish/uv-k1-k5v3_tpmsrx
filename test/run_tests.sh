#!/usr/bin/env bash
# Host tests for dectpms. Needs gcc or clang; not part of the firmware build.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-gcc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "dectpms host tests"
"$CC" -std=c11 -Wall -Wextra -O2 -o "$TMP/host_dectpms" "$HERE/host_dectpms.c"
"$TMP/host_dectpms"

PYTHON=${PYTHON:-python3}
echo "variant / RF / layout checks"
"$PYTHON" "$HERE/test_variants.py"

echo "style / doc-block checks"
"$PYTHON" "$HERE/test_style.py"

echo "tpmsrx host tests passed"
