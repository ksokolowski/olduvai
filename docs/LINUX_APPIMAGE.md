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
| libmt32emu (MT-32 music) | bundled (built from munt source — not in Ubuntu; `dlopen`'d; needs your own ROMs to sound) |
| HD fonts (Freckle Face, Noto Sans; OFL) | bundled beside the binary |
| ALSA (`libasound`) | host-provided (it `dlopen`s host plugins) |
| GM SoundFont | host-provided (auto-discovered; see below) |
| Game files | user-provided (`--game-dir` / config / GOG) |

## General MIDI music — best sound

Unlike Windows (which always has the Microsoft GS Wavetable Synth backed by the
Roland `gm.dls` set), Linux has no built-in GM synth, so the engine renders GM
with FluidSynth and a SoundFont it discovers. Auto-discovery order (an explicit
`--soundfont <file>` or the `soundfont` config key always overrides):

1. `~/.config/olduvai/soundfonts/`  (a font here beats every system copy — but
   it must use one of the four names below; an arbitrarily-named file is not
   discovered, use `--soundfont` for that)
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

## Do not test the raw binary against a newer host

`build/appimage/olduvai` is compiled inside the pinned jammy container, so it
was built against **that** SDL2 and FluidSynth. Running it directly on a newer
distro links it to the host's much newer copies instead — a combination that
never ships, because the AppImage bundles the libraries it was built against.

Measured on Ubuntu 26.04 (FluidSynth 2.4.8), same MIDI, same SoundFont:

| what was run | GM renders |
|---|---|
| container-built binary, bare on the host | **segfaults ~50% of runs** |
| natively built binary | 8/8 clean |
| the shipped AppImage (bundled FluidSynth) | clean |

So an intermittent GM crash while poking at `build/appimage/olduvai` is an
artefact of the test setup, not a defect — and it cost an afternoon once. Test
the **AppImage**, or build natively.

## Limitation — glibc floor

An AppImage runs only on distros whose glibc is **>= the build host's**, so the
build environment decides who can run the result.

The released AppImage targets **glibc 2.35** — Ubuntu 22.04, Linux Mint 21,
Debian 12 and newer, and any Steam Deck (SteamOS has been 2.37+ since 3.5).
That is pinned by building inside a digest-locked `ubuntu:22.04` container
rather than by a runner label, because runner images are retired on GitHub's
schedule: `ubuntu-22.04` begins deprecation 2026-09-17 and is removed
2027-04-17, and the natural repair — bumping to `ubuntu-24.04` — would raise
the floor to 2.39 and drop those users without anyone noticing.

The floor is **asserted, not assumed**: `build_appimage_linux.sh` reads the
highest `GLIBC_` version required by every ELF it is about to pack and fails
if it exceeds the declared maximum. Raising `OLDUVAI_GLIBC_MAX` drops distros,
so it is a reviewed edit, not a fix for a red build.

Not covered by a 2.35 floor: RHEL/Alma/Rocky 9 (2.34) and Debian 11 (2.31).
Going lower is not simply a matter of an older container — Debian bullseye
fails on four independent counts, including `linuxdeploy` itself requiring
GLIBC_2.34.
