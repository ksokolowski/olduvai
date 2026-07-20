# Linux — self-contained AppImage

A single portable `olduvai-x86_64.AppImage` that runs the game with no install.
Ships **no game content** — supply your own game files at runtime.

## Build

```sh
./packaging/build_appimage_linux.sh      # → ./olduvai-x86_64.AppImage
```

All CLI flags pass through the AppImage exactly as for a plain binary —
`./olduvai-x86_64.AppImage --profile hd --game-dir ~/games/prehistorik --play`
works as expected. A double-click launch from a file manager passes no
flags (that is how desktop launches work on every Linux desktop): the
first-run dialog and the in-game Options (persisted to
`~/.config/olduvai/play.json`) cover that path.

First run fetches `linuxdeploy` and `appimagetool` into `build/appimage/tools/`
(network needed once). ImageMagick (`magick`) is required for the icon.

## Run

```sh
./olduvai-x86_64.AppImage --game-dir /path/to/prehistorik/files --play
```

A GOG install is auto-discovered, so a GOG copy plays with a bare
`./olduvai-x86_64.AppImage --play`.

## What's in the bundle

| Component | Source |
|---|---|
| Engine + SDL2 (+ transitive deps) | bundled (ldd-driven) |
| OPL music (vendored Nuked-OPL3) | built in |
| libfluidsynth (GM music) | bundled (injected — it is `dlopen`'d) |
| HD fonts (Freckle Face, Noto Sans; OFL) | bundled beside the binary |
| ALSA (`libasound`) | host-provided (it `dlopen`s host plugins) |
| GM SoundFont | host-provided (auto-discovered; see below) |
| Game files | user-provided (`--game-dir` / config / GOG) |

## General MIDI music — best sound

Unlike Windows (which always has the Microsoft GS Wavetable Synth backed by the
Roland `gm.dls` set), Linux has no built-in GM synth, so the engine renders GM
with FluidSynth and a SoundFont it discovers. Auto-discovery order (an explicit
`--soundfont <file>` or the `soundfont` config key always overrides):

1. `~/.config/olduvai/soundfonts/`  (drop any SoundFont here to force it)
2. system dirs — `/usr/share/sounds/sf2`, `/usr/share/soundfonts`,
   `/usr/share/scummvm` — preferring, in order:
   `Roland_SC-55.sf2` → `GeneralUser-GS.sf2` → `FluidR3_GM.sf2` → `default-GM.sf2`.

**For the most authentic sound** (the Roland Sound Canvas voice, same lineage as
the Windows `gm.dls`), install ScummVM's GPLv3 SoundFont — on Debian/Ubuntu:

```sh
sudo apt install scummvm-data     # provides /usr/share/scummvm/Roland_SC-55.sf2
```

The engine then auto-selects it with no configuration. A purely
clean-provenance alternative is **GeneralUser GS** (freely redistributable);
drop `GeneralUser-GS.sf2` in `~/.config/olduvai/soundfonts/`.

## Limitation — glibc floor

An AppImage runs only on distros whose glibc is **>= the build host's**. Built
on a bleeding-edge system it runs on bleeding-edge distros only. For broad
portability, build inside an old-baseline container (e.g. Ubuntu 22.04) — a
planned follow-up, along with CI that builds the AppImage on tag.
