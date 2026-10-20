#!/bin/bash
# Converts the bundled (or an explicitly supplied) Platform Racing 3 iOS app into everything the Xcode project needs:
#   - PR3Remake/Resources/GameData     original data files (art, levels, sounds) + PR3.code (the armv7 code image)
#   - PR3Core/generated                the original game code statically recompiled to C
# usage: tools/build_game.sh ["/path/to/Platform Racing 3.ipa"]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IPA="${1:-$ROOT/original/Platform Racing 3.ipa}"
if [ ! -f "$IPA" ]; then
  echo "IPA not found: $IPA" >&2
  echo 'Place your copy at original/Platform Racing 3.ipa or pass its path as the first argument.' >&2
  exit 1
fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
unzip -q "$IPA" 'Payload/PR3.app/*' -d "$TMP"
APP="$TMP/Payload/PR3.app"
DATA="$ROOT/PR3Remake/Resources/GameData"
rm -rf "$DATA"; mkdir -p "$DATA"
rsync -a --exclude PR3 --exclude '*.plist' --exclude Seb1971 --exclude PkgInfo --exclude MainWindow.nib --exclude FBConnect.bundle "$APP"/ "$DATA"/
cp "$ROOT/audio/MenuTheme.mp3" "$DATA/MenuTheme.mp3"
lipo "$APP/PR3" -thin armv7 -output "$DATA/PR3.armv7"
VENV="$ROOT/tools/.venv"
if [ ! -x "$VENV/bin/python" ]; then python3 -m venv "$VENV"; fi
"$VENV/bin/pip" install -q capstone pillow
PY="$VENV/bin/python"
"$PY" "$ROOT/tools/fix_data.py" "$DATA"                  # repairs defects of the original data (empty propeller-hat colour layer)
"$PY" "$ROOT/tools/import_art.py" "$DATA"                # redrawn hats with proper colour layers (art/hats)
"$PY" "$ROOT/tools/fix_feet.py" "$DATA"                  # penguin second foot, turkey / donkey feet closer together
"$PY" "$ROOT/tools/fix_parts.py" "$DATA"                 # align selector and gameplay part artwork without redraws
"$PY" "$ROOT/tools/make_unlocked_art.py" "$DATA"      # main-menu tile of the "Experimental" single-player mode
export PR3_BIN="$DATA/PR3.armv7"
export PR3_SYMS="$TMP/syms.txt"
python3 "$ROOT/tools/recomp/mksyms.py" "$PR3_BIN" "$PR3_SYMS"
python3 "$ROOT/tools/recomp/bind.py"
rm -rf "$ROOT/PR3Core/generated"
"$PY" "$ROOT/tools/recomp/gen.py" "$ROOT/tools/recomp/full.json" "$ROOT/PR3Core/generated"
# Keep only the loadable segments and hide the Mach-O magic: the app loads this as plain data (see rt_init)
python3 - "$DATA/PR3.armv7" "$DATA/PR3.code" <<'PY'
import sys
d = bytearray(open(sys.argv[1], 'rb').read()[:1167360 + 57344])
d[0:4] = bytes(b ^ 0xff for b in d[0:4])
open(sys.argv[2], 'wb').write(d)
PY
rm "$DATA/PR3.armv7"
echo "Done. Open PR3Remake.xcodeproj (run tools/gen_xcodeproj.py first if you added files)."
