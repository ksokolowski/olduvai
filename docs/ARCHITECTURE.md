# Olduvai architecture

## Layering

```
src/
  formats/        — pure decoders: CUR/LZSS archives, MAT sprites, PC1
                    images, DUR collision, MDI music, VOC samples.
                    No SDL, no filesystem — bytes in, structures out.
  prepare/        — game-file detection (checksums) and EXE table readers.
  core/           — game state, constants, RNG, collision bitmap.  No SDL.
  systems/        — player physics, monster AI, spawning, collisions,
                    screen transitions, cave/secret logic.  Headless.
  presentation/   — SDL2 rendering, audio, input, window/scaling.  Split
                    into seven subdirectories (below); the loop drivers and
                    the zero-fan-out hubs stay at the top level.
    render/       — everything that writes pixels: tile/sprite/background
                    compose, HUD, boss and widescreen presenters,
                    transitions, frame presenter.
    menu/         — the menu machine: model/nav/render, settings staging
                    and apply, pause overlay service, dialogs, text editor,
                    the F5 report form.
    audio/        — OPL music/SFX, MIDI sequencing, host MIDI (MT-32 and
                    General MIDI), resampling.  No outbound edges.
    input/        — gamepad, autofire, per-frame input, replay/record.
    level/        — level setup and per-level save/restore.
    sequence/     — non-interactive sequences: intro/end cinematics, the
                    L3 trunk descent, loading and tally screens.
    diag/         — not part of playing the game: F5 bug capture, draw
                    log, reinit test hook, the asset viewer.
  trace/          — JSONL frame-trace emitter (validation harness client).
  app/            — main loop, CLI (main.cpp; trace_main.cpp is the trace
                    harness binary).
packaging/        — platform packaging: Info.plist/rc templates, dmg/
                    AppImage/zip build scripts.
tests/            — doctest unit tests.  Decoder tests run on synthetic
                    hand-authored fixtures; tests that need real game
                    files skip automatically when the files are absent.
```

Lower layers never include from higher layers. `formats`, `core`, and
`systems` are SDL-free by construction. Both rules are enforced by
`scripts/check_layers.sh` in CI, which derives the layer from the second
path field — so the `presentation/` subdirectories above do not affect it.

## Configuration

Config file at the platform config dir (`~/.config/olduvai/play.json`),
with named game profiles:

- `dos` — the faithful original DOS experience (engine defaults).
- `hd`  — enhanced presentation (upscaling, smooth motion, vector HUD);
  audio picks the best available backend and degrades gracefully.

Precedence: engine defaults → profile → top-level config keys → CLI flags.
An in-game settings menu (writing the same file) is planned with the
presentation layer; it is the only settings surface on handhelds.

## Validation

The engine is developed against a private reference implementation that
serves as a behavioural oracle: shared input scripts are replayed through
both engines and their per-frame JSONL traces are diffed (player position,
state, energy, lives, screen, RNG state — zero tolerance, both engines use
integer arithmetic). Decoder output is additionally byte-compared against
the reference implementation's output for the same input files.

## Targets

| Tier | Target |
|---|---|
| 1 | Linux x86_64 |
| 2 | macOS (Apple Silicon first; Intel expected-working, untested) |
| 3 | ARM Linux retro handhelds (SDL2-era toolchains) |
| 4 | Windows (MinGW cross-build) |

C++17 is the language ceiling (handheld toolchains). Dependencies: SDL2 and
vendored in-tree libraries (`third_party/`: doctest, stb, RtMidi, Nuked-OPL3,
fonts) — no git submodules.
