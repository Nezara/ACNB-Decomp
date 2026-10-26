# ACNB - Decomp

**Early prototype / proof of concept.** Expect rough edges.

A PC source port of **Armored Core: Nine Breaker** (PS2, USA release SLUS-21200), built from the game's logic
decompiled into readable C++. It runs natively on Windows: no PS2 emulator, no MIPS interpreter, no emulated
GS / VU / IOP. The game's own code (menus, AC simulation, weapons, AI, sound driver) runs as native C++ on a small
platform layer (OpenGL renderer, SDL3 input / audio, disc and memory card access).

This is "Stage 0": a frozen reference snapshot of the port, played as the original game logic does it.

## What's included

- **Versus mode**: boot / title -> Versus garage -> Versus setup -> match (1 or 2 players, COM opponents, teams).
- **Garage test arena**, **Arena ranking matches** (Arena top / search / match / result / ranking) and **Paint**
  (paint, emblem editor, colour editor).
- The native sound driver (the game's IOP sound modules, decompiled).
- Memory card saves (file-backed virtual cards).
- A replay player and a per-frame logical state hash (both off unless you ask for them; used for testing).
- Fixed settings in this build: interlace off, blur off, all parts unlocked.

## What's not included

- **No game data.** You need your own disc image of the USA release; the image is checked at launch.
- **Training mode** (the Training page on the top menu does nothing) and other menus outside the scope above.
- Movies (FMV playback is skipped).
- Network / LAN and i.LINK play.
- The development tooling (the PS2-emulating test build, checks, decompilation tools).

Reaching a function that is not decompiled yet stops the game with exit code 70 and names the function.

## Running it (release zip)

1. Download `ACNB-Stage0-win64.zip` from the Releases page and unzip it. Keep the folder layout.
2. Put your disc image in the `game` folder (any `.iso` name works).
3. Run `sourceport\build\acnb.exe`.

Window / audio: `--set video.scale=2`, `--set video.window=1920x1440`, `--set video.aspect=16:9`,
`--set audio.volume=50`, `--mute`. Gamepad and keyboard are supported.

## Building from source

Needs Windows, Python 3, and a `tools` folder with llvm-mingw (i686 clang) and SDL3 in the folder names that
`sourceport\build.ps1` uses (not included).

    powershell -NoProfile -File sourceport\build.ps1            (-Release for -O2, -Clean for a full rebuild)

More detail (replays, state hash, settings): `README_STAGE0.md`.

## Notes

- Armored Core and Nine Breaker are trademarks of their owners. This project contains no game assets and is not
  affiliated with or endorsed by them.
