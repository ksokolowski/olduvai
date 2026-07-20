# Olduvai architecture

## Layering

```
src/
  formats/        — pure decoders: CUR/LZSS archives, MAT sprites, PC1
                    images, DUR collision, MDI music, VOC samples.
                    No SDL, no filesystem — bytes in, structures out.
  prepare/        — first-run pipeline: game-file detection (checksums),
                    table readers, local cache writer/loader.
  core/           — game state, constants, RNG, collision bitmap.  No SDL.
  systems/        — player physics, monster AI, spawning, collisions,
                    screen transitions, cave/secret logic.  Headless.
  presentation/   — SDL2 rendering, audio, input, window/scaling.
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
`systems` are SDL-free by construction (enforced by review; a CI include
lint is planned).

## First-run prepare-and-cache pipeline

Olduvai ships no game-derived data. At first run it reads the user's own
game files and prepares a local cache:

| Stage | Trigger | Key | Output |
|---|---|---|---|
| 1 — prepare | first run / cache miss | game-file checksums + pipeline version | bucket + manifest (decoded-asset cache deferred — see below) |
| 2 — HD bake | enhanced mode enabled | content hash of stage-1 pixels + algorithm + scale | persisted upscaled blocks on disk |

Cache lives under the platform cache dir, resolved by `src/prepare/
cache_paths`:

- Linux:   `$XDG_CACHE_HOME/olduvai` (default `~/.cache/olduvai`)
- macOS:   `~/Library/Caches/olduvai`
- Windows: `%LOCALAPPDATA%\olduvai\cache`
- `$OLDUVAI_CACHE_DIR` overrides all of the above (tests, power users).

Layout: `<root>/<key>/manifest.txt` is the stage-1 bucket for one game-file
set (key = FNV-1a/64 of the five files' digests + sizes + pipeline version);
`<root>/hd/<contenthash>.bin` are stage-2 HD blocks (raw RGBA + a 16-byte
header).  Nothing derived from game files is ever written into the
repository or an installation directory.

CLI: `--prepare` force-builds the bucket; `--verify-cache` reports
present/valid/stale (exit 0 only when valid); `--purge-cache` deletes the
whole cache root.  `--play` calls `ensure_prepared` first, printing
"Preparing game data…" on a miss/stale and reusing silently otherwise.

**Stage-1 decoded-asset cache is deferred (scaffolded).**  A persisted
decode cache (LZSS/MAT/PC1 → native pixels) is only safe if a cache-load is
byte-identical to a fresh decode; that round-trip is not yet proven and the
runtime decode is cheap, so stage 1 currently writes only the keyed bucket +
manifest (reserving the key scheme) and the engine still decodes assets on
the fly.  Stage 2 (HD bake) persistence IS live: HD blocks are cosmetic and
content-addressed, so a disk hit reproduces the exact upscale output and can
never change gameplay or rendered frames — it only skips recompute.  The HD
disk layer is gated behind enhanced/HD mode (`HdAssetCache::enable_disk`).

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
