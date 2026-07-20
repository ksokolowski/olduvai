# Changelog

All notable changes to Olduvai. Versioning follows semver once 1.0.0
lands; 0.x releases are beta.

## Unreleased

## 0.9.2 — 2026-07-19

- The Windows zip and the Linux AppImage now ship `data/menus.json`
  beside the binary (user-customisable menu model; the compiled-in copy
  remains the fallback) — silences the startup "no data/menus.json on
  disk" notice.
- Config saves are no longer silent on failure: any problem writing
  play.json reports the exact path and reason to the console, and the
  one-time style choice prints where it was saved.
- Windows: settings now actually persist — play.json lives in
  `%APPDATA%\olduvai` (plain Windows launches set no HOME, so the config
  path degraded to a launch-dir-relative `.config` that never
  round-tripped; first Windows field test).
- GOG auto-discovered installs now get the one-time Classic DOS /
  Enhanced HD question too — it previously existed only inside the
  missing-files dialog, so a found GOG copy silently started classic.
- Host-MIDI / GM music (the Windows Microsoft GS default) no longer
  plays 15-25% slow: the MIDI pump discarded sub-millisecond remainders
  every iteration, compounding at fine timer resolutions.  OPL music was
  unaffected (audio-sample-clocked).
- First-run dialog fix: choosing "Enhanced HD" now applies to the session
  that answered the dialog — previously the choice was only saved for the
  NEXT launch, so the first session ran classic DOS (AppImage report).
- In-game Style → Enhanced now takes effect on Apply: the settings
  classifier evaluates the preset's keys as a set (no single key crosses
  the classic↔HD boundary alone, so the whole preset used to classify as
  "persist-only" and visibly did nothing), and the pause-menu reinit now
  carries the enhanced master flag through the rebuild.
- Settings that do only apply on the next launch now say so in the
  confirm dialog ("Saved - takes effect on next launch") instead of
  applying silently.
- CLI fix: an explicit `--aspect keep` no longer loses to a saved
  widescreen config.
- Docs: AppImage flag passthrough clarified (flags work from a terminal;
  a double-click launch passes none by desktop design).
- Style preset / Cave Painting toggles now respect Apply & Discard: the
  `enhance.*` flags used to be written to play.json the moment the Style
  row changed (before the confirm dialog), so Discard kept them — a
  discarded "Enhanced HD" click still booted fully enhanced on the next
  launch. They now stage with the rest of the settings session at all
  three Options sites (pause, title, boss pause): nothing is saved until
  Apply, Discard reverts cleanly, and a preset click that matches the
  current style stages nothing at all.
- Applying `enhance.*` changes also updates the running session (the
  flags used to only be saved for the next launch); features latched at
  level entry catch up on the next reload.
- The granular save no longer overwrites the enhanced master flag when
  both change in one Apply (the "enhanced=false" companion write is
  suppressed while the master rides the same session).
- Title menu: vector text/HUD glyphs now switch immediately when a Style
  Apply rebuilds the window (previously classic bitmap glyphs lingered
  until Start Game).

## 0.9.1 — 2026-07-19

- F5 bug reports now land in `~/olduvai/bug_reports` by default instead of
  whatever directory the game was launched from; override with the
  `bug_report_dir` key in play.json or the `OLDUVAI_BUG_DIR` environment
  variable. The mid-descent WS-SHOT capture follows the same root.
- Gameplay data tables (cave widths, secret-food scores, AdLib SFX voice
  patches) are now read from the user's own executable at startup instead
  of being compiled into the engine — content-policy hardening; behaviour
  is bit-identical (golden-trace verified).
- Build system moved to CMake presets: one tree per flavour under `build/`
  (`release`, `asan`, `fuzz`; packaging uses `build/universal` and
  `build/appimage`). Test binaries land in `build/<preset>/tests/`, dev
  tools in `build/<preset>/tools/`. Source layout: app code lives in
  `src/app/`, platform packaging in `packaging/`, the menu model in
  `assets/data/`.
- Bug reports are a single self-sufficient report.md (Markdown) per capture:
  the machine-readable state.json is gone (its only consumer was the private
  triage tool; it was orphaned in public clones). report.md carries the state
  summary, F5 annotations, embedded screenshots and entity table; score/timer/
  frame added so nothing was lost. Fixed the engine version in reports
  (OLDUVAI_VERSION was only defined on the exe, not the library where the
  report writer lives — every report said 0.0.0).
- F5 bug capture gained an in-engine form: tag + reproducibility (left/right
  choice rows, per-tag description templates) and a multi-line description
  edited in a full-canvas text editor with soft word-wrap (Tab focus ring: text / Save / Cancel;
  Esc cancels; caps at 2000 chars / 60 lines). Leaving the form opens the
  same confirm dialog as Options-Apply — Save writes the report (the
  annotations land in report.md), Discard drops it. Built on a new reusable `text` menu-row type + a pure,
  unit-tested TextEditor model + SDL_StartTextInput plumbing.

- One-click presentation preset: a "Style" row (Classic DOS / Enhanced HD /
  Enhanced 4:3) at the top of Options — title menu, pause and boss pause
  alike — fans the full profile through the normal staging/confirm/apply
  machinery. This also makes the classic↔HD switch actually possible from
  the GUI (the enhanced master flag was previously CLI/config-only), and
  the first-run dialog now asks the question once, right after the game
  folder is picked.

- Windows builds now use MSVC (the shipped toolchain): static `/MT` CRT (no
  VC++ redistributable), SDL2 shipped as `SDL2.dll`. A small dynamic-loader
  shim (`dynlib.hpp`) gives the optional MT-32/FluidSynth backends a
  `LoadLibrary` path on MSVC (MinGW/POSIX keep `dlopen`); the backends also
  gained Windows `.dll` names so they load on Windows at all. Both the gitea
  private Windows runner lane and the GitHub release job build with MSVC.

- The missing-game-files report is always emitted to the console too (the
  GUI dialog is additional, not a replacement) — a debugging/log trail for
  every launch mode. Build instructions moved from the README to a proper
  per-platform docs/BUILDING.md (Linux/macOS/Windows-MSYS2, packaging
  scripts, build options); the README keeps a quick-start pointer.

- First-run GUI experience: double-clicking the app now defaults to playing
  (it previously ran the terminal-only detection report — "nothing
  happened"), and when game files are missing it opens a native dialog:
  locate your game folder (native picker; validated and remembered in
  play.json) or open the GOG store page. Terminal launches keep the text
  report.

- macOS releases are now ONE universal dmg (Apple Silicon + Intel in a fat
  binary; SDL2 built universal from a version-pinned source build). Both
  slices verified: full unit + SDL suites and the 300-frame golden trace
  pass on x86_64 (Rosetta) as well as native arm64.

- Build hygiene: the default build now produces the game binary only; test
  binaries and developer tools moved behind explicit `tests` and `tools`
  targets (`cmake --build build --target tests|tools`).

- Every project source file now carries the 2-line SPDX license header
  (GPL-3.0-or-later + copyright), CI-enforced; LEGAL.md states the project
  copyright explicitly.

- Main menu: the Start Game row now has a level selector — left/right cycles
  Level 1-7, Enter starts the chosen level directly (Level 1 keeps the
  classic title flow). Menu model gains action-rows-with-values nav
  semantics, unit-tested; the default render is pixel-identical (golden
  unchanged).

- README rewritten around the project's mission — preserving Prehistorik and
  keeping it playable for both the players who remember it and new
  generations. New "Getting the game" section (GOG release first-class:
  PREH.SQZ + install auto-discovery; original floppy-era DOS files equally
  welcome). The legal position moved to a dedicated bullet-form LEGAL.md;
  the README keeps a four-bullet summary instead of restating it in six places.

- Gamepad support (SDL2 GameController): hotplug, d-pad + left stick,
  configurable button mapping via `pad_*` settings keys; pads drive
  gameplay, menus, intro and tally skips alike.
- Release pipeline: GitHub Actions builds the Linux AppImage, Windows
  portable zip and macOS dmg on every version tag and
  publishes them as a draft GitHub Release with SHA256SUMS; README gains a
  Downloads section with the unsigned-binary launch notes. Every artifact
  carries third-party license texts (`licenses/` + THIRD-PARTY-NOTICES.md;
  Nuked-OPL3's LGPL-2.1 text now vendored).
- The bone is the official project logo: shown in the README, embedded in
  olduvai.exe as the Windows icon (Explorer/taskbar/title bar), and set as
  the runtime SDL window icon on every window (fixes the blank alt-tab /
  taskbar icon under Linux window managers; also the macOS Dock while
  running).
- Full project logo: the fire-styled "OLDUVAI" wordmark (rendered by the
  engine's own menu-title code — HdText/Freckle Face + the caveman
  fire-and-blood shade — via the new `olduvai_logo` dev tool) with the bone
  beneath, plus transparent bone variants. All graphics consolidated under
  `assets/` with a provenance README (the only images we ship; all original
  creative work, enforced by check_tree); README header now shows the logo.
- The app presents as "Olduvai" (capital O) everywhere: an Info.plist
  section embedded in the bare CLI binary names the macOS app menu / Dock
  (previously the lowercase executable filename), and SDL_HINT_APP_NAME
  covers the Linux WM_CLASS / Wayland app-id and audio stream name.

## 0.9.0 — 2026-07-05 (first beta)

The full game is playable natively end-to-end, frame-validated against
the reference implementation (12-scenario cross-engine corpus +
300-frame golden trace, shared-RNG lockstep).

### Engine / fidelity
- All seven levels, three boss fights, caves, secret rooms, flight
  sequences, score tallies, game over and the win ending.
- Byte-faithful `dos` profile: PIT-exact 18.2065 Hz pacing
  (absolute-deadline scheduler), VGA hold-frame scanout (default on,
  with refused-vsync degradation), fullscreen integer scaling.
- Known original bugs preserved and annotated; the few deliberate
  deviations are individually documented in-repo.

### Enhanced presentation (optional, off in `dos`)
- HD sprite upscaling: OmniScale, xBR, MMPX, Eagle, smooth, retro.
- True widescreen: live level margins (animated spawn-post monsters),
  panorama screen transitions, bezel fills; `hd-43` keeps 4:3.
- Smooth 60 FPS motion interpolation; vector text + enhanced HUD.
- Animation extensions: 3-frame cave descent, cave-emerge reveal,
  teleport cloud sequences (STOP-SIGN + level-start materialization),
  L3 descent dust package, boss victory polish.
- Quality of life (all profiles): tally-roll skip, silent loading
  screen, dream-screen attract hold with direct menu, cursor auto-hide.

### Audio
- OPL/AdLib built in (vendored Nuked-OPL3); MT-32 (libmt32emu) and
  General MIDI (FluidSynth) runtime backends; host MIDI out.
- Data-driven SFX catalog following the selected backend; seamless
  music looping.

### Shell
- Declarative in-game menus (title, pause + live-apply Options,
  save/load v3, cheats, boss pause), shared model with the reference.
- Config file + profiles (`dos` / `hd` / `hd-43`), `--save-config`.
- Sequencer-position CLI: `--level 0` (intro) … `1-7` (levels) … `8`
  (ending).

### Tooling
- F5 in-game bug capture, input record/replay, draw-call log, debug
  overlays, god mode, headless screenshot hooks, golden-trace CI gate.
