# Linux: the self-contained AppImage

A single portable `olduvai-x86_64.AppImage` that runs the game with no install.
It ships **no game content**; supply your own game files at runtime.

## Build

```sh
./packaging/build_appimage_linux.sh      # → ./olduvai-x86_64.AppImage
```

All CLI flags pass through the AppImage exactly as for a plain binary:
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
| FluidSynth (GM music) | built in (vendored; needs a SoundFont to sound) |
| libmt32emu (MT-32 music) | built in (vendored; needs your own ROMs to sound) |
| HD fonts (Freckle Face, Noto Sans; OFL) | bundled beside the binary |
| ALSA (`libasound`) | host-provided (it `dlopen`s host plugins) |
| GM SoundFont | host-provided (auto-discovered; see below) |
| Game files | user-provided (`--game-dir` / config / GOG) |

## General MIDI music: the best sound

Unlike Windows (which always has the Microsoft GS Wavetable Synth backed by the
Roland `gm.dls` set), Linux has no built-in GM synth, so the engine renders GM
with FluidSynth and a SoundFont it discovers. Auto-discovery order (an explicit
`--soundfont <file>` or the `soundfont` config key always overrides):

1. `~/.config/olduvai/soundfonts/` (a font here beats every system copy, but
   it must use one of the four names below; for any other name, use
   `--soundfont`)
2. the system dirs `/usr/share/sounds/sf2`, `/usr/share/soundfonts` and
   `/usr/share/scummvm`, preferring, in order:
   `Roland_SC-55.sf2` → `GeneralUser-GS.sf2` → `FluidR3_GM.sf2` → `default-GM.sf2`.

**For the most authentic sound** (the Roland Sound Canvas voice, same lineage as
the Windows `gm.dls`), install ScummVM's GPLv3 SoundFont. On Debian/Ubuntu:

```sh
sudo apt install scummvm-data     # provides /usr/share/scummvm/Roland_SC-55.sf2
```

The engine then auto-selects it with no configuration. A purely
clean-provenance alternative is **GeneralUser GS** (freely redistributable);
drop `GeneralUser-GS.sf2` in `~/.config/olduvai/soundfonts/`.

## Do not test the raw binary against a newer host

`build/appimage/olduvai` is compiled inside the pinned jammy container, so it
was built against **that** SDL2. Running it directly on a newer distro links
it to the host's SDL2 instead, a combination that never ships: the AppImage
bundles the one it was built against. Test the **AppImage**, or build natively.

## Limitation: the glibc floor

An AppImage runs only on distros whose glibc is **>= the build host's**, so the
build environment decides who can run the result.

The released AppImage targets **glibc 2.35**: Ubuntu 22.04, Linux Mint 21,
Debian 12 and newer, and any Steam Deck (SteamOS has been 2.37+ since 3.5).
That is pinned by building inside a digest-locked `ubuntu:22.04` container
rather than by a runner label, because runner images are retired on GitHub's
schedule: `ubuntu-22.04` entered deprecation on 2026-09-17 and is removed
2027-04-17, and the natural repair (bumping to `ubuntu-24.04`) would raise
the floor to 2.39 and drop those users without anyone noticing.

The floor is **asserted, not assumed**: `build_appimage_linux.sh` reads the
highest `GLIBC_` version required by every ELF it is about to pack and fails
if it exceeds the declared maximum. Raising `OLDUVAI_GLIBC_MAX` drops distros,
so it is a reviewed edit, not a fix for a red build.

It asserts a second floor the same way: the C++ runtime. Since 0.9.7 the
AppImage needs **`GLIBCXX_3.4.30`** (the libstdc++ of GCC 12) because the
threaded upscaler waits on a `std::condition_variable`, which GCC 12
re-versioned. No supported user is dropped: Ubuntu 22.04 and Mint 21 ship
exactly this runtime, and every distro meeting the glibc 2.35 floor ships
GCC 12 or newer.

Not covered by a 2.35 floor: RHEL/Alma/Rocky 9 (2.34) and Debian 11 (2.31).
An older container does not fix that: Debian bullseye
fails on four independent counts, including `linuxdeploy` itself requiring
GLIBC_2.34.
