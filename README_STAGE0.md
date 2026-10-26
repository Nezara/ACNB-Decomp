# Armored Core: Nine Breaker - Stage 0 source port

Stage 0 is a frozen copy of the source port: the game logic as decompiled (port/src, port/lib, the native IOP sound
modules in port/src/iop) on the platform layer that runs it (port/platform, port/render, sourceport/main), with the
development version's additions stripped. It is the reference build for later work (phase 4 branches from it): when a later
build plays a replay differently, Stage 0 shows what the original logic does.

What is not here (it stays in the unpublished development version): the hybrid build (port/runtime, the interpreter bridge,
launcher.exe), all check tooling (rcheck / bcheck / regress / hrcheck / rqcheck), self-tests, debug probes, the host
link (network play), the sandbox script authoring engine, the decomp tooling (Ghidra scripts, maps, notes, plan).

## Fixed tweaks (no switches)

- Interlace off: every field store is the even field (no half-line shimmer on a progressive display).
- Blur off: no battle blur passes, no menu copy blur / flicker filter (display only; the game state is unchanged).
- All parts unlocked (`game.unlock_parts=1`): every part is in the garage, after the default parts are set and after
  a memory card load (sourceport/main/host_opt.cpp; the game's own debug "all parts" routine DF_MDBG_PartsAll).
  This is the default (live play and new recordings, header `game.unlock_parts=1`). A replay plays at the
  `game.unlock_parts` its settings header recorded, and at 0 when it has no header (the old ones), so the dev
  tree's replays stay in step (all 5 dev goldens identical).

## Inputs

- `game/`: the disc image. Stage 0 looks for `game\Armored Core - Nine Breaker (USA).iso`, else the first `*.iso` in
  `game\`; the image's SHA-1 is checked at launch. A hard link to the ISO elsewhere on the same drive or a plain copy
  both work. `disc.path` in port/build/acnb.ini or the `ACNB_ISO` environment variable override it.
- `tools/` (not included): llvm-mingw (i686 clang) + SDL3, in the folders sourceport/build.ps1 names. A link or a
  plain copy works. Python 3 is needed for the build.
- Memory cards: `port/memcard/mc0` (your card, created on first save; not versioned). The check cards
  `port/build/garage_card_orig`, `blade_card_orig`, `pcsx2fpu_card_orig` (copies, not versioned) are what the replays
  start from; the runners copy them per run, never the real card.

## Build

    powershell -NoProfile -File sourceport\build.ps1          (debug -O1; -Release for -O2, -Clean for a full rebuild)

Writes sourceport\build\acnb.exe (+ SDL3.dll, the sandbox name tables sbx_names.tsv / sbx_types.tsv and
, the list of functions not decompiled yet: reaching one exits with code 70 and names it).

## Run

    sourceport\build\acnb.exe                               play (window, gamepad / keyboard)
    acnb.exe --set video.scale=2 --set video.window=1920x1440 --set video.aspect=16:9
    acnb.exe --set audio.volume=50 | --mute | --set audio.enable=0
    acnb.exe --set input.gamepad=N / input.keyboard=0|1 / input.pad2=0|1 / input.deadzone / input.trigger / input.rumble

Other options: `--headless` (no window, no audio), `--fast` (no 59.94 Hz pacing), `--exit-after N`, `--shot N`
(PNG of vblank N into ACNB_SHOT_DIR), `--record F` (record your input as a replay; never overwrites, F-2.. instead).

## Play a replay

    acnb.exe --replay port\build\battle1.txt [--headless --fast --exit-after 10950]

Format v2 replays drive both pads; format v3 (sandbox replays, port/build/sbx_*.txt) also carry their poke / freeze
events and the card they need (their header names it; the run refuses another card with exit 2). Bounded runs with a
fresh card copy and a forced close after the timeout:

    powershell -NoProfile -Command "& port\tools\sprun.ps1 -Tag t -Replay battle1 -Exit 10950 -Timeout 200"
    ... -Replay sbx_ene_t4 -Exit 2210 -Window -Card <copy of port\build\garage_card_orig>

## State lines (logical state hash)

`ACNB_STATE_HASH=1` prints one `state vb N ...` line per vblank (port/statehash/fields.txt = what is hashed;
ACNB_STATE_DUMP=vb,vb + ACNB_STATE_DUMP_DIR write the field values). `ACNB_RQ_HASH=1` prints `rq frame` render hashes.
Off by default; nothing else changes when they are on.

    port\tools\sprun.ps1 -State ...                          -> <Out>\<Tag>.st.txt / .rq.txt
    port\tools\statecheck.ps1 -Replay battle1 -Self          determinism (this build twice)
    port\tools\statecheck.ps1 -Replay battle1 -Other <exe>   another acnb.exe vs this one (first difference + fields)
    port\tools\statecheck.ps1 -Make                          goldens -> port\statehash\golden (at freeze)
    port\tools\statecheck.ps1 -Verify [-GoldenDir D]         this build vs the goldens
    python port\tools\stcmp.py A.st.txt B.st.txt | python port\tools\rqcmp.py A.rq.txt B.rq.txt
