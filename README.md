# PR3Remake — Platform Racing 3 on modern iPhones

This is the **original 2010 iOS game, ported** — not a re-creation. The game's own armv7 code (levels, physics,
rendering, menus, items, effects: everything the original app did offline) is statically recompiled to C and runs
inside a small native iOS shell (SwiftUI + OpenGL ES + OpenAL). Art, levels and sounds are the original files.

You need your own copy of the original app, `Platform Racing 3.ipa` (32-bit, iOS 4.3). Nothing from it is
included here and none of it may be redistributed.

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

## Build

1. `tools/build_game.sh "/path/to/Platform Racing 3.ipa"` — extracts the data and generates `PR3Core/generated` (~10 s).
2. `python3 tools/gen_xcodeproj.py` — writes `PR3Remake.xcodeproj` (re-run if you add source files).
   For a real device set your team and a unique bundle id: `PR3_TEAM=ABCDE12345 PR3_BUNDLE_ID=com.you.pr3 python3 tools/gen_xcodeproj.py`
3. Open `PR3Remake.xcodeproj`, choose the simulator or your iPhone (set **Signing → Team** if you did not pass `PR3_TEAM`), Run.

Requirements: Xcode 15+, Python 3 (the scripts create `tools/.venv` with Capstone). Landscape only, iOS 16+.
Generated C is compiled with `-ffp-contract=off` on purpose: the original's `vmla` rounds the product before the
add, and fused multiply-adds would change the physics.

Controls are the original's: the on-screen thumbstick (left/right/up/down), item button at the bottom right,
tap the menus. Progress is saved by the game itself in the app's Documents folder (`Settings.dat`).

## What works

- Everything the original did without its servers: title, menus, the character screen, all 50 single-player levels
  with the blocks, items, water, teleports, effects, level text, minimap, countdown, timer and medals they use;
  progress saves; sound and music (OpenAL; checked at the API level — sources play without errors — not by ear).
- Custom (user-made) blocks: the original asked the Sparkworkz server for them; the app's own offline fake
  server serves the block files that shipped in the IPA, and the art is drawn by the original code.
- Online play, chat, leaderboards, Facebook/Twitter/Tapjoy: the servers are gone; those paths fail cleanly.

## Verification (why you can trust the port)

`tools/verify/run_all.sh "/path/to/Platform Racing 3.ipa"` re-runs everything below (~15 min):

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
