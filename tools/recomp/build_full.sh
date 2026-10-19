#!/bin/bash
# Builds a macOS command-line test runner from PR3Core (runtime + generated game code).
# usage: build_full.sh <test.c> <out_exe>      (env: EXTRA_CFLAGS, LINK_FLAGS, OBJ_DIR)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$HERE/../../PR3Core"
G="$R/generated"
OBJ="${OBJ_DIR:-/tmp/pr3_test_obj}"
mkdir -p "$OBJ/gen" "$OBJ/rt"
# generated code: rebuild only when newer than its object
export R G OBJ EXTRA_CFLAGS
find "$G" -name "gen_*.c" -print0 | xargs -0 -P 8 -n 1 bash -c 'f="$0"; o="$OBJ/gen/$(basename "$f").o"; if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$R/include/arm_rt.h" -nt "$o" ]; then clang -c -O1 -w -ffp-contract=off $EXTRA_CFLAGS -I "$R/include" -I "$G" "$f" -o "$o"; fi'
rm -f "$OBJ"/rt/*.o
for f in "$R"/runtime/*.c "$1"; do
  clang -c -O1 -w -ffp-contract=off ${EXTRA_CFLAGS:-} -I "$R/include" -I "$G" "$f" -o "$OBJ/rt/$(basename "$f").o"
done
clang -o "$2" "$OBJ"/rt/*.o "$OBJ"/gen/*.o ${LINK_FLAGS:-}
