# ACNB source port (Versus mode)

The true PC source port: decompiled game logic + a native platform layer + a modern renderer.
**No PS2 emulation lives here, ever**: no MIPS interpreter, no GS/VU/VIF/DMA/IOP emulation, no identity-mapped
PS2 memory.

## How it relates to `../port`

| | `port/` (hybrid, dev oracle) | `sourceport/` (this folder, the product) |
|---|---|---|
| Purpose | Runs the original game with decompiled functions swapped in one by one, and checks each one bit-exact against the original MIPS | The real PC game, built only from decompiled code |
| Runs the full game | Yes, today | Not until Versus is fully decompiled (many sessions) |
| Contains emulation | Yes, as test scaffolding (deleted at the end) | Never |

There is **one copy of the decompiled game code**. It is written and verified in the hybrid, then compiled
into both builds. Nothing is copied between the folders by hand.

## Layout

```
sourceport/
  build.ps1        build -> build/acnb.exe (x64 later, i686 while struct layouts must match the PS2)
  main/            entry point (wmain -> the game's main()), sp_missing reporter
  platform/        (planned) SDL window, input, audio out, file access (ISO / extracted data), save files
  render/          (planned) render queue (std::vector of draw commands) + OpenGL frontend, GLM math
  build/ output, not versioned: acnb.exe, missing.md (build report), gen/ (generated stubs), obj-*/
```

## Building and running

```
.\build.ps1          # -Release for -O2, -Clean to rebuild everything
.\build\acnb.exe                  # needs the disc image (below)
.\build\acnb.exe --disc-selftest  # the file layer on the disc vs the hybrid's boot trace: PASS / FAIL
.\build\acnb.exe --window-test 120 --shot 60   # SDL3 window + rq GL backend, empty frames (clear colour), no disc
```

The game run opens the window (`port/platform/video`: SDL3, GL 4.5 core, 59.94 Hz pacing, F11 / Alt+Enter fullscreen)
before `main()`; `--headless` runs without it (null render backend). Options: `--shot N` (shot_N.png into
`ACNB_SHOT_DIR`), `--exit-after N`, `--set video.scale=N`, `--set video.aspect=W:H` (the only settings at milestone 1).

Which disc image (both builds, `port/platform/disc_boot.h`): env `ACNB_ISO` (scripts), else `[disc] path` in
acnb.ini (the launcher `port/build/acnb_launcher.exe` sets it with Browse...; acnb.exe reads an acnb.ini next to
itself, else `<root>\port\build\acnb.ini`, never writes it; a relative path starts at the ini's folder), else
`<root>\Armored Core - Nine Breaker (USA).iso`. Nothing is read from `extracted\` at run time (dev scripts only).

At launch the exe checks the disc image (USA SLUS-21200 only: SYSTEM.CNF + SHA-1 of the ELF and both overlays) and
loads the game's binary data tables from its ELF; exit codes 2 = no / bad
image, 3 = another game, 4 = modified version, 5 = table error. Overlay globals (OLMENU / OLSORTIE files) live in
linker groups that Overlay_Load resets to their start-up values (../port/platform/ovlreg.h).

The build compiles every decompiled file from `../port/src` and `../port/lib` with `-DACNB_SOURCEPORT` (the
porting layer `port/include/acnb/port.h` turns the oracle helpers into plain C++), then `scripts/sp_missing.py`
generates a stub for every function and data object that nothing defines yet, and writes **`build/`**:
Versus progress per module, the missing functions the native code calls (by original source file), and the game
data that is still zero-filled but initialized in the original. A stub prints the function name and ends the
program (exit code 70), so running the exe shows how far the native code gets. Today: the game's `main()` itself
is still missing.

## What you can test along the way

1. **Progress report (from the start).** The build links every decompiled function and lists what is still
   missing for Versus mode (unresolved functions per module), so progress is visible before anything runs.
2. **Viewer modes (render phase).** Game data drawn through the new renderer straight from the ISO: models,
   an assembled AC from your save, stages. These are early visual checkpoints of the new renderer.
3. **Native battle, then the full Versus flow** (garage -> setup -> match) once the logic is complete.

Third-party libraries (SDL3, GLM) get added on first use, after the user approves the downloads.
