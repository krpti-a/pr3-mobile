#!/bin/bash
# Re-runs the port's verification against your copy of the original IPA:
#   1. instruction-level differential test: every instruction encoding the game uses runs on random machine states in
#      Unicorn (an ARM emulator running the ORIGINAL code) and in the recompiled C; registers, flags, VFP/NEON state
#      and memory must be identical;
#   2. level survey: all 50 single-player levels are started from the menu in a headless macOS build and played for ~25 s.
# usage: tools/verify/run_all.sh "/path/to/Platform Racing 3.ipa" [work dir]
set -euo pipefail
IPA="${1:?path to Platform Racing 3.ipa}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="${2:-/tmp/pr3_verify}"; mkdir -p "$W"
VENV="$ROOT/tools/.venv"
if [ ! -x "$VENV/bin/python" ]; then python3 -m venv "$VENV"; fi
"$VENV/bin/pip" install -q capstone unicorn pillow
PY="$VENV/bin/python"

rm -rf "$W/ipa"; unzip -q -o "$IPA" 'Payload/PR3.app/PR3' -d "$W/ipa"
lipo "$W/ipa/Payload/PR3.app/PR3" -thin armv7 -output "$W/PR3.armv7"
python3 "$ROOT/tools/recomp/mksyms.py" "$W/PR3.armv7" "$W/syms.txt"

echo "== 1. instruction-level differential test =="
rm -rf "$W/gen"
"$PY" "$ROOT/tools/verify/collect.py" "$W/PR3.armv7" "$W/syms.txt" "$W/gen" 100000
"$PY" "$ROOT/tools/verify/run.py" "$W/PR3.armv7" "$W/gen" 6 1

echo "== 2. level survey =="
EXTRA_CFLAGS="-DRT_GL_REAL" LINK_FLAGS="-framework OpenGL -framework CoreFoundation" OBJ_DIR="$W/obj" \
  "$ROOT/tools/recomp/build_full.sh" "$ROOT/tools/recomp/tests/gl_boot.c" "$W/gl_boot"
rm -rf "$W/res"; cp -R "$ROOT/PR3Remake/Resources/GameData" "$W/res"
python3 - "$W/res/LevelList.dat" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for l in d: l['medalsRequired'] = '0'       # unlock every level for the test
json.dump(d, open(sys.argv[1], 'w'))
PY
rm -rf "$W/survey"; mkdir -p "$W/survey"
python3 "$ROOT/tools/verify/survey.py" "$W/gl_boot" "$W/PR3.armv7" "$W/res" "$W/survey" 0 49 4
echo "Frames are in $W/survey/L<nn>/ (PPM, portrait; rotate 90 degrees to view)."
