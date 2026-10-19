#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
OUT=$(mktemp "${TMPDIR:-/tmp}/pr3_hats_test.XXXXXX")
trap 'rm -f "$OUT"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I "$ROOT/PR3Core/include" \
    "$ROOT/tools/recomp/tests/pr3_hats_test.c" \
    "$ROOT/PR3Core/runtime/pr3_hats.c" \
    -o "$OUT"
"$OUT"
