# PR3Remake — Platform Racing 3 on modern iPhones

This is the **original 2010 iOS game, ported** — not a re-creation. The game's own armv7 code (levels, physics,
rendering, menus, items, effects: everything the original app did offline) is statically recompiled to C and runs
inside a small native iOS shell (SwiftUI + OpenGL ES + OpenAL). Art, levels and sounds are the original files.

The original `Platform Racing 3.ipa` (32-bit, iOS 4.3) is included at `original/Platform Racing 3.ipa`, so the
build works from this checkout. You can also pass a different IPA path to `tools/build_game.sh`.

## How it works

```
Platform Racing 3.ipa
   │  tools/build_game.sh
   ├──► PR3Remake/Resources/GameData     original data files + PR3.code (the armv7 __TEXT/__DATA image)
   └──► PR3Core/generated                4,022 game functions recompiled to C (ARM → C, ~480k lines)

PR3Core/runtime      what the iPhone OS used to provide, re-implemented for the recompiled code:
                     libc/libm/libstdc++ 4.2 ABI, a heap, the Objective-C runtime subset the game uses
                     (retain/release/autorelease, NSTimer run loop, NSDate …), OpenGL ES 1.1 and OpenAL
                     pass-through, WAV AudioFile, file access, a fake offline "server" the game already contained.
PR3Remake            SwiftUI app → GameViewController: 30 fps CADisplayLink, touch forwarding, letterboxed
                     480×320 landscape view, GL layer.
```

The recompiler (`tools/recomp/recomp.py`, Capstone-based) turns every ARM instruction into C, keeping registers in
C locals; VFP and the NEON subset the game uses are supported. Calls into the OS (`bl _glDrawArrays`,
`objc_msgSend` …) go to the runtime above. The game thinks it is still running on a 2010 iPhone: it still draws a
320×480 portrait framebuffer rotated by 90°, counts frames at 30 fps and talks to its original touch API.

## Build and run

From the repository root, run these commands in order:

```sh
./tools/build_game.sh
python3 tools/gen_xcodeproj.py
open PR3Remake.xcodeproj
```

`build_game.sh` reads `original/Platform Racing 3.ipa` by default. To use a copy elsewhere, run
`./tools/build_game.sh "/path/to/Platform Racing 3.ipa"` instead. It extracts the original game data and generates
`PR3Core/generated`; the project generator then adds those files to Xcode. In Xcode, select the **PR3Remake** scheme,
choose an iOS Simulator or your iPhone, and press **Run**. For a physical iPhone, select your Apple development team
under **Signing & Capabilities**, or set it before generating the project:

```sh
PR3_TEAM=ABCDE12345 PR3_BUNDLE_ID=com.you.pr3 python3 tools/gen_xcodeproj.py
```

If you add or remove source files, rerun `python3 tools/gen_xcodeproj.py` before building.

Requirements: Xcode 15+, Python 3 (the scripts create `tools/.venv` with Capstone). Landscape only, iOS 16+.
Generated C is compiled with `-ffp-contract=off` on purpose: the original's `vmla` rounds the product before the
add, and fused multiply-adds would change the physics.

Controls are the original's: the on-screen thumbstick (left/right/up/down), item button at the bottom right,
tap the menus. Progress is saved by the game itself in the app's Documents folder (`Settings.dat`).

## Game modes and saves

The main menu has two single-player tiles (swipe the row): **Single Player** (the original, progress as in 2010) and
**Experimental**, a second single-player profile with every hat, head, body and feet part and every level open. Each
profile has its own save file in the app's Documents folder (`Settings.dat`, `Settings_unlocked.dat`), written by the
game itself when the app leaves the foreground and every 10 seconds. The tile is added by wrapping two original
functions (`PR3Core/runtime/pr3_modes.c`, `wraps` in `tools/recomp/full.json`); its artwork is built by `tools/make_unlocked_art.py` (background: `art/menu/experimental-bg.webp`).

## Main branch additions

The `original` branch is the faithful port plus bug fixes only. `main` adds, on top of it:

- **Widescreen gameplay**: in a level the view grows to the display's aspect ratio (world, background and block culling fill the
  screen, controls sit at the edges, outside the notch); menus keep the original 480×320 area. `PR3Core/runtime/pr3_wide.c`, `hle_gl.c`.
- **Controls** (`PR3Remake/Shell/ControlsOverlay.swift`, `GamepadInput.swift`, `PR3Core/runtime/pr3_controls.c`): joystick or
  d-pad, sideways-only steering, optional jump / crouch (super jump) buttons, item button; see-through when idle, touch areas
  larger than the drawn buttons; settings behind the gear in a level. Game controllers (GameController framework) steer, jump,
  crouch and use items, and drive a cursor in the menus.
- **Experimental** profile (all hats, parts and levels), redrawn hats and the Donkey part (`art/`, `tools/import_art.py`).
- **Faster loading**: the original's LodePNG hot paths are replaced by zlib / plain C (`pr3_speed.c`), output bit-identical.
- **Santa Hat**: the block landed on takes on ice-block physics and an ice-block sprite for 50 frames; the sprite fades before the block returns to normal (`pr3_hats.c`).

## What works

- Everything the original did without its servers: title, menus, the character screen, all 50 single-player levels
  with the blocks, items, water, teleports, effects, level text, minimap, countdown, timer and medals they use;
  progress saves and sound effects. The supplied menu theme plays in menus and character customization, then pauses
  during levels. The IPA contains no level-music tracks; tap the music-note button in a level to import an audio file
  for offline level playback. Audio plays with the phone's Silent switch on.
- Custom (user-made) blocks: the original asked the Sparkworkz server for them; the app's own offline fake
  server serves the block files that shipped in the IPA, and the art is drawn by the original code.
- Online play, chat, leaderboards, Facebook/Twitter/Tapjoy: the servers are gone; those paths fail cleanly.

## Verification (why you can trust the port)

`tools/verify/run_all.sh "original/Platform Racing 3.ipa"` re-runs everything below (~15 min):

1. **Instruction-level differential test** (`tools/verify/collect.py`, `run.py`). All 49,807 distinct non-branch
   instruction encodings in the game's code are executed on random registers, flags, VFP/NEON state and memory in
   **Unicorn running the original code** and in the recompiled C; every register, flag, FP register and byte of
   memory must match. Result: ~298,000 runs, 0 mismatches. (Along the way it found four recompiler bugs — the
   sign of negative post-index offsets (which had broken a `std::sort` while loading levels), halfword/register
   post-index write-back, shifter carry-out of flag-setting logic ops, and flag updates by `adc`/`sbc` — all fixed.)
2. **Level survey** (`tools/verify/survey.py`): the headless macOS build (same `pr3_boot/pr3_frame/pr3_touch`
   API as the app, real OpenGL, virtual clock) starts all 50 single-player levels through the menus and plays them;
   frames and engine warnings are collected. No unknown calls, bad addresses or crashes.
3. A 9,000-frame soak run holds ~100 MB RSS and a flat object count (refcounted objects, per-frame autorelease pool).

Debugging aids: `PR3_TRACE=addr,… PR3_TRACE_FIELDS=…` (build with `-DRT_TRACE`), `PR3_TEXLOG`, `PR3_DUMP_READ`,
`GLOG`, `PR3_DRAW_LIMIT` in the headless harness (`tools/recomp/tests/gl_boot.c`), and a debug-build autopilot:
launch the simulator app with `-script "tap@14:240:160;tap@18:240:195;hold@46-200:100:236"` (seconds, 480×320 coordinates).

## Layout

| Path | |
|---|---|
| `PR3Remake/` | Swift shell, asset catalog (icon from the original artwork), `Resources/GameData` (generated) |
| `PR3Core/runtime/` | runtime / HLE layer (C) |
| `PR3Core/generated/` | recompiled game (generated, not committed) |
| `tools/recomp/` | recompiler, symbol/bind extraction, headless test harness |
| `tools/verify/` | differential test, level survey, `run_all.sh` |
| `tools/build_game.sh`, `tools/gen_xcodeproj.py` | build pipeline |

Not an official product. Platform Racing 3 and its assets belong to their respective owners.
