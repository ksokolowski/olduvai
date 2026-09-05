# Changelog

All notable changes to Olduvai. Versioning follows semver once 1.0.0
lands; 0.x releases are beta.

## Unreleased

## 0.9.6 — 2026-09-05

- Fixed: the L4 boss arena now spawns the fighter where the original does
  (found by the new cross-engine fight scenarios; `boss_l4_victory`'s golden
  was re-baselined with the reason in its header).
- Fixed: `--render-audio` with `--music-device opl` rendered silence — the
  offline path never handed the OPL player a track. It now renders real FM.
- Added: L4 and L6 boss-fight replay gates, both verified frame-identical
  against the reference engine, plus a smooth-motion L6 slam gate.
- Added: `--render-sfx <id|all>` renders AdLib sound effects offline (digest
  or WAV) — the SFX counterpart of `--render-audio`.
- Added: `--aspect 4:3` and `--aspect stretch` render coverage; the win
  ending's climb animation is now photographable (`OLDUVAI_ENDING_SHOT_FRAME`)
  and gated at two points.
- Changed: CLI parsing reorganized internally; no flag or message changed.
- Changed: in-session display switches re-derive the present path reliably in
  both directions (classic↔enhanced), verified by an extended reinit gate.

## 0.9.5 — 2026-07-29

Audio reaches every platform for the first time, and the things that were
supposed to prove it started actually proving it.

- **There are two presentation styles now, not three.** "Enhanced 4:3" is
  gone from the Style menu; there is Classic DOS and Enhanced HD, and the
  aspect is chosen in Video where the other display settings live. It was
  never really a third style — it was Enhanced HD with one display setting
  changed. `--profile hd-43` still works and does what it always did.

  Picking Enhanced no longer overrides an aspect you chose deliberately: if
  you had 4:3 or Stretch set, it stays. Coming from Classic it still gives you
  widescreen.

- **Fixed: switching from Enhanced to Classic mid-game kept the smooth
  motion**, and with it suppressed the classic VGA scanout that Classic mode
  is supposed to use. Starting the game in Enhanced and switching to Classic
  from the Options menu left it in a half-and-half state; starting in Classic
  was always correct. Both are right now.

- **The menus are built into the game rather than read from a file.** The
  engine no longer looks for `data/menus.json` at startup, and the packages no
  longer ship it — the menu layout is compiled in when the game is built. If a
  menu definition is ever wrong, the build now fails instead of the game
  starting up without menus. (The file is still how menus are authored, and is
  where translations would go; it is simply read when the game is built rather
  than when you run it.)

- **Fixed: the main menu was missing on a bare install.** If the game ran
  without its `data` folder beside it, the title screen had no main menu at
  all and the game would not get past the intro — while the in-game pause menu
  and the boss pause menu both worked fine, because only the main menu had
  been left without its built-in fallback copy.

- **Fixed: six command-line options were silently ignored when you had a
  saved setting.** `--music-device`, `--sfx-backend`, `--display-mode`,
  `--transitions`, `--hd-font` and `--banner-fx` lost to `play.json` whenever
  the value you typed happened to be the default one — so there was no way to
  say, for example, "ignore my saved AdLib setting and pick the best audio
  device" (`--music-device auto`), because that request looked identical to
  not asking at all. The command line now wins whenever you actually type the
  option, and your saved settings still apply when you do not.

- **Fixed: a corrupt or truncated game file crashed instead of explaining
  itself.** If one of your game files was damaged — a partial copy, a bad
  download — the engine aborted with `libc++abi: terminating due to uncaught
  exception` and nothing else. It now exits cleanly and tells you which file
  and what is wrong with it, for example *"archive truncated: data for entry
  BONUS.VOC"*, and suggests re-copying it from your original media.

- **Enhanced mode is now all-or-nothing.** The seven per-effect toggles
  (smooth motion, HD text, HUD overlay, cinematic cue, fluid bubbles, secret
  slide, descent pan) are gone from the Options menu, and `--enhance <list>`
  no longer picks a subset — any name you list simply turns enhanced mode on.
  If you had some effects off, they come back on; nothing else changes, and
  classic DOS mode is untouched.

  The reason is the entry above. Seven switches is 128 combinations, and only
  two were ever tested: everything off and everything on. The other 126
  shipped with no coverage at all, and at least one of them visibly broke the
  pause menu. Offering a choice we could not check was worse than not
  offering it. If you want HD without the smooth sub-frame motion,
  `--transitions classic` still does exactly that.

- **Fixed: the Level 6 victory drop was not smoothed in enhanced mode.** After
  the giant is beaten, the caveman slides down to the floor — and that slide
  stepped in visible jerks while everything else in enhanced mode moved
  smoothly. The interpolation was in fact running; it just could not be seen,
  because it moved the sprite in whole original-resolution pixels. At the
  enhanced render scale that is sixteen screen pixels per step, so the three
  in-between frames it drew could only land on three of them. It now positions
  the falling sprite the same way the boss fight positions the player, which
  has always been smooth, so the drop finally looks like the rest of the game.

- **Fixed: the Dark Woods trunk-descent cinematic ran slowly in enhanced mode.**
  The level-end sequence where the platform grinds down the giant trunk played
  noticeably slower than it should, then snapped back to normal speed the
  moment it finished. In enhanced mode the two descent phases were drawing
  every frame **three times over** — the same image, pixel for pixel, each one
  re-running the full HD upscale of the whole widescreen picture. Three times
  the most expensive work in the renderer, for something already on screen.
  On a fast machine that was merely wasteful; on a real display each redundant
  redraw missed the screen refresh it was aiming at, and the delay tripled.

  Nothing about how the cinematic looks has changed — the frames it shows are
  the same frames, in the same order, for the same total time. It simply stops
  drawing each of them twice more. The camera pan in the middle genuinely does
  move between sub-frames, and it still renders every one of them.

- **Fixed: in widescreen, the ending was stretched — and stayed stretched.**
  Finishing the game in widescreen showed the win ending horizontally
  distorted, and the distortion did not stop there: it carried on through the
  logo intro that follows, all the way back to the main menu. The ending and
  the game-over screen are drawn at the original picture width, but the game
  had left the display on the wider widescreen canvas, so that narrow picture
  was pulled across the full width instead of being placed inside it. Nothing
  reset the canvas until the main menu did, which is why it outlasted the
  ending itself. The boss fights already restored the picture shape before
  their own end sequences; now the rest of the game does too.

- **Fixed: the Level 4 triceratops lost its rider for one frame.** After
  beating the Level 4 boss, the caveman rides the triceratops off — and on the
  very last frame before the screen faded out, he vanished while the dinosaur
  carried on alone. That frame is also the image the fade works from, so the
  fade began from a picture with nobody on the mount. The rider is now held on
  through the final frame, so the ride reads continuously into the fade.

- **Fixed: things near the screen edges disappeared during cave transitions in
  widescreen.** Entering or leaving a cave with widescreen enabled made
  scenery close to the left and right edges — the spikes on Level 1's second
  screen are the clearest example — flicker away or come out clipped for the
  length of the fade, then return correctly once it finished. In widescreen
  the game rebuilds the strips either side of the main picture from the level
  layout, and during a transition that rebuild painted over the objects
  already drawn there. Those strips now keep their contents for the whole
  transition.

- **Fixed: the pause menu could come out shredded in widescreen.** With
  widescreen on and enhanced HD text off — reachable by unticking "HD text" in
  the Options menu, or with `--enhance smooth-motion` — the pause menu's text
  was drawn diagonally across the screen instead of inside its panel. The menu
  is drawn into a wider-than-normal buffer in widescreen, and the bitmap text
  routine still assumed the standard 320-pixel width, so every line of pixels
  landed in the wrong place and anything past the old width was cut off. The
  boss pause menu had the same fault. Nothing was wrong with your settings —
  the combination simply had no test covering it, because every existing
  screenshot test runs with HD text on, which uses a different text path.
- **MT-32 and General MIDI now work on macOS and Windows.** Both synths were
  loaded at runtime and only ever shipped in the Linux AppImage, so macOS and
  Windows users silently fell back to AdLib — music still played, so nothing
  looked wrong. MT-32 emulation is now built into the binary on every
  platform, and General MIDI ships beside it. The Linux build carries the same
  self-contained FluidSynth as the others instead of the distribution's, which
  also makes the AppImage smaller (49 bundled libraries down to 39).
- **General MIDI now finds your SoundFont on macOS and Windows.** The search
  only ever looked in Linux locations, so installing a SoundFont the
  documented way (`brew install scummvm`) left GM silent with no explanation.
- **MT-32 now finds your ROMs on Windows.** The search looked in `$HOME`,
  which Windows does not normally set, so the only place that worked was a
  `mt32-roms` folder next to `olduvai.exe` — fine for the portable zip, but
  there was nowhere to put ROMs that survived unzipping a new version over the
  old one. It now looks in `%LOCALAPPDATA%\olduvai\mt32-roms` first, the same
  place it already looked for SoundFonts. The folder beside the executable
  still works, and nothing changes on macOS or Linux.
- **MT-32 now works on Linux with mixed-case ROM filenames.** The lookup
  required every ROM in a set to share one letter case; combining a legacy
  dump with a MAME-versioned one — the common case — matched nothing, and
  MT-32 was simply unavailable. macOS and Windows were unaffected only
  because their filesystems ignore case.
- **You can now choose the MT-32 model**, and the game says which it loaded:
  `--mt32-model auto|cm32l|mt32`. With both ROM sets present it used to pick
  CM-32L silently, so two machines could sound different with nothing to
  explain why.
- **When a music backend cannot start, the game now says why** — whether the
  synth is missing or only the SoundFont/ROMs are, and where it looked. The
  old message listed every possibility at once, which made a broken install
  indistinguishable from an unconfigured one.
- **Switching Enhanced ↔ Classic while fullscreen is now instant.** It used to
  drop out of fullscreen to a black window for several seconds on every
  switch.
- **The macOS app is simpler and starts reliably on current macOS.** It no
  longer carries a separate SDL2 library, and the packaging step that produced
  an app which would hang on macOS 26 is gone.
- **The HD upscale cache no longer fills your disk.** Enhanced mode wrote
  every upscaled asset to a cache directory that had no size limit, no
  eviction and no expiry, so it only ever grew: one machine reached **411 MB
  across 2680 files** simply by trying the different HD profiles, of which the
  actual prepared game data was 4 KB.

  It is gone, and the reason is that it was never earning its place. The cache
  existed to avoid repeating the upscale on every launch — worth doing when
  that work is slow, as it was in the Python reference implementation this
  behaviour was proven against first. It is not slow here. Starting with a
  completely empty cache, using the most expensive upscaler at widescreen, the
  worst single frame took **25 ms against a 55 ms budget** and no frame was
  ever late; upscaling as you go simply does not cost you anything you can
  see. The engine already keeps each upscaled asset in memory for the rest of
  the session, so the disk copy was only ever saving work between runs — work
  that turns out to be free.

  Removed with it: `--prepare`, `--verify-cache`, `--purge-cache` and
  `--decode-sfx`, which existed only to manage or fill that directory. If you
  have a cache directory from an earlier version, this release deletes it for
  you on first start — for some people that is several hundred megabytes.
- The Linux AppImage keeps a **stated compatibility floor** (glibc 2.35 —
  Ubuntu 22.04, Mint 21, Debian 12 and newer), now enforced by the build
  rather than inherited from whichever machine happened to build it.

## 0.9.4 — 2026-07-26

- **Pause overlay in Enhanced widescreen** now shows the game properly
  behind it. The menu used to black out the widescreen side margins and
  drop the HUD entirely; on boss arenas the frozen fight also fell back to
  unfiltered classic pixels while the menu above it stayed sharp. The
  paused frame now keeps its margin scenery, its HUD, and its HD
  filtering, and the boss HUD is drawn across the wide band as it is
  during play.
- Pausing mid-swing no longer eats the club. The frozen frame was still
  advancing the swing counter once per rendered frame, so holding the
  menu open — or opening the F5 report form — made the club vanish.
- **Dark Woods (L5) end screen** had black widescreen bars instead of
  forest and floor. It is the one surface screen with no neighbour on
  either side, and it fell out of the widescreen path entirely.
- Leaving a **cave** briefly painted widescreen scenery onto the cave
  screen itself for the length of the fade. Affected every regular cave,
  L1 through L7.
- Changing the **HD profile** from Options now applies everywhere at once.
  The pause overlay, loading and tally screens kept upscaling with the
  previous profile until the next level.
- `--god` now applies to boss fights (99 lives), and toggling god from the
  pause Cheats menu survives a level change instead of silently reverting
  at the next level.
- **The HD cache on disk is roughly 40x smaller.** The upscaled bake stored
  16-colour artwork as uncompressed 32-bit pixels; it is now compressed
  (~2% of the former size — a 400 MB cache becomes ~10 MB). Existing
  caches stay readable and shrink as entries are rewritten; no re-prepare
  is needed.
- Problems reading gameplay tables from the game executable are now
  reported instead of being swallowed, so a corrupt file says so rather
  than surfacing later as a confusing loader error.
- Internal: the Dark Woods trunk-descent sequence moved into its own unit
  and shared helpers replaced several copied ones — behaviour-preserving
  and verified frame-for-frame. New regression gates cover the boss arena,
  the HD/widescreen pause frames, the cave transitions and the trunk
  descent; the tree also gained a clang-tidy configuration.

## 0.9.3 — 2026-07-23

- Terminal launches now print a one-line style hint when no Classic /
  Enhanced choice has been made yet (`pick a style with --profile dos|hd`).
  The one-time question is GUI-only, so a plain `olduvai --play` from a
  shell used to start classic DOS with no sign that a choice existed.
- Internal: substantial engine cleanup with **no change to how the game
  plays.** The largest source files were untangled into focused units —
  sprite blitting, boss-arena rendering, static-background compose, the
  score subsystem, the collision resolver's phases, and the CLI argument
  parser — each moved behaviour-preserving and verified frame-for-frame
  against the reference. The code is cleaner; play is byte-identical to
  0.9.2.
- Windows: the executable now carries version metadata (Explorer →
  Properties → Details: product name, version, description, copyright) —
  embedded from the CMake project version on both the MinGW and MSVC
  builds.
- Added a code-signing policy ([SIGNING.md](SIGNING.md)). Windows binaries
  remain unsigned for now (SignPath Foundation application pending) —
  verify downloads against each release's `SHA256SUMS.txt`.

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
  `assets/` with a provenance README (the only images shipped; all original
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
