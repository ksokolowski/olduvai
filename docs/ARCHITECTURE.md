# Olduvai architecture

## Layering

```
src/
  formats/        — pure decoders: CUR/LZSS archives, MAT sprites, PC1
                    images, DUR collision, MDI music, VOC samples.
                    No SDL, no filesystem — bytes in, structures out.
  prepare/        — game-file detection (checksums), archive access and
                    EXE table readers.
  core/           — game state, constants, RNG, collision bitmap.  No SDL.
  systems/        — player physics, monster AI, spawning, collisions,
                    screen transitions, cave/secret logic, the frame step
                    (frame_runner).  Headless.
  enhance/        — the optional HD layer: pixel upscalers (MMPX, xBR,
                    OmniScale, the Scale2x/3x and Eagle family), the HD
                    asset cache, vector text and the vector HUD, and the
                    RGBA draw target they share (canvas.hpp).  No SDL.
  presentation/   — SDL2 rendering, audio, input, window/scaling.  The loop
                    drivers (game_app, boss_app), the session (game_session)
                    and the display (pipeline.hpp, display.hpp) stay at the
                    top level; the rest is split by concern:
    render/       — everything that writes pixels: tile/sprite/background
                    compose, HUD, boss and widescreen presenters, the frame
                    presenter.
    level/        — a platform level's state, setup, save/restore, and its
                    view (LevelView, TickRender).
    boss/         — a boss fight's state, view, pause and ending.
    menu/         — the menu machine: model/nav/render, settings staging
                    and apply, the pause service, dialogs, the text editor.
    audio/        — OPL music/SFX, MIDI sequencing, MT-32 / General MIDI /
                    host MIDI, resampling.  No outbound edges.
    input/        — gamepad, autofire, per-frame input, replay/record.
    sequence/     — non-interactive sequences: screen transitions, intro/end
                    cinematics, the L3 trunk descent, loading and tally
                    screens.
    diag/         — not part of playing the game: F5 bug capture and form,
                    draw log, frame stats, the headless menu script, the
                    asset viewer.
  app/            — main, CLI and config resolution (main.cpp);
                    trace_main.cpp is the trace harness binary.
packaging/        — platform packaging: Info.plist/rc templates, dmg/
                    AppImage/zip build scripts.
tests/            — doctest unit tests and shell tests.  Decoder tests run on
                    synthetic hand-authored fixtures; tests that need real
                    game files skip automatically when the files are absent.
```

Lower layers never include from higher layers:
`formats < prepare < core < systems < enhance < presentation < app`.
`formats`, `core`, `systems` and `enhance` are SDL-free by construction.
Both rules are enforced by `scripts/check_layers.sh` in CI, which derives
the layer from the second path field, so the `presentation/`
subdirectories above do not affect it.

## A level at run time

```
run_game → GameSession        the window, renderer and audio: the Pipeline
  ├─ run_platform_level(…, pipe)                    presentation/game_app.cpp
  │    LevelDisplay = Display<LevelView, LevelViewDeps>
  │      LevelSurface          renderer textures, HD scale, logical size
  │      LevelView             widescreen presenter, gameplay buffer,
  │                            banners, frame presenter, TickRender,
  │                            text screens, transitions
  └─ run_boss_level(…, pipe)                        presentation/boss_app.cpp
       BossDisplay  = Display<BossView, BossViewDeps>
```

- **Pipeline** (`pipeline.hpp`): the session's window and renderer, its
  audio device, and `adopt(settings, aspect)`, which may replace any of
  them.  Read through it at use time; never keep a renderer or a device.
- **Display** (`display.hpp`): a level's surface, opened first (bind-time
  decisions key on the vector-HUD gate), and the view over it.
  `rebuild()` is the in-place display reinit: the view and the surface go
  first (their textures belong to the renderer `adopt` may destroy), then
  the new surface, a reload of what the display decided at bind time, and
  the new view.  The level itself stays: no gameplay state is touched.
- **One tick**: the driver runs `docs/FRAME_LOOP.md` in order.  The
  gameplay steps are `systems::run_frame` and `run_post_frame_steps`,
  headless.  Then `TickRender`: `compose` (the one compose that advances
  draw state), `advance_once` (banner arm, teleport, HUD, GET READY),
  `present` (or smooth motion's interpolated sub-frames); then
  `TickPacer::end_tick` (the vsync fill, the VGA scan, or the DOS tick's
  deadline).
- **The pause verdict**: `PauseService` owns input while the overlay is
  open and draws nothing.  `verdict()` returns the frame's intent in a
  fixed order (quit, restart, load, warp, reinit, abort); `kFroze` presents
  the paused frame and skips the tick; `kReinitDisplay` makes the driver
  call `Display::rebuild`, then `pipeline_changed()`.  A fight's
  `BossPause` has a smaller menu (resume, restart, quit, options); its
  Apply raises `wants_reinit()` and takes the same rebuild.
- **Drawing**: overlay and HD draws (vector text, the HUD, menus) write
  into an `enhance::Canvas` and place things with a `Rect`; alpha goes
  through the one `blend_pixel` / `blend_rect`.
- **Output**: `LevelSurface` owns what reaches the window: the 320*s
  texture, the wide texture (made at the width the presenter's margin rule
  asks for), the overlay and the logical size.  Every present path runs
  the same four steps on it: `upload(px, native_w, Res)`, `show` /
  `show_pillarboxed`, `overlay_pass`, and `present_output` (so
  `OLDUVAI_DUMP_OUTPUT` sees every frame).  The presenters keep only what
  they compose: the level's neighbour peeks, the boss's mirrored arena,
  the descent's scrolled margins.

## Configuration

Settings live in the platform config dir (`~/.config/olduvai/play.json`, a
flat JSON file).  Precedence: engine defaults → `--default-profile` (a
launcher's device defaults) → `play.json` → `--profile` → CLI flags.

Profiles (`presentation/menu/profile_table.hpp`):

- `dos`: the faithful original DOS experience (engine defaults).
- `hd`: enhanced presentation (upscaling, smooth motion, vector HUD);
  audio picks the best available backend and degrades gracefully.
- `dos-handheld`, `hd-handheld`: the same two families, the enhanced one
  tuned for handheld screens and CPUs.

The in-game Options menu stages every change and writes `play.json` on
Apply; it is the only settings surface on handhelds.

## Validation

The engine is developed against a private reference implementation that
is a behavioural oracle: shared input scripts are replayed through
both engines and their per-frame JSONL traces are diffed (player position,
state, energy, lives, screen, RNG state; zero tolerance, both engines use
integer arithmetic). Decoder output is additionally byte-compared against
the reference implementation's output for the same input files.

Code size is held by a ratchet: every function over a `.clang-tidy` size or
cognitive-complexity threshold is listed, with a reason, in
`scripts/complexity_baseline.txt`, and `scripts/check_tidy.sh --ratchet`
fails a new entry or any moved number.

## Targets

| Tier | Target | Accuracy |
|---|---|---|
| 1 | Linux x86_64, macOS (Apple Silicon; Intel untested), Windows (MSVC) | byte-exact goldens and the trace corpus |
| 2 | ARM Linux retro handhelds (SDL2-era toolchains) | classic mode as tier 1; enhanced modes may trade accuracy for speed |

C++17 is the language ceiling (handheld toolchains). Dependencies: SDL2 and
vendored in-tree libraries (`third_party/`: doctest, stb, RtMidi,
Nuked-OPL3, libmt32emu; fonts under `assets/fonts/`), no git submodules.
FluidSynth is loaded at run time when present.
