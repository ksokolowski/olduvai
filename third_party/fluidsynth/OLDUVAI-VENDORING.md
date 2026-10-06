# FluidSynth — vendored

The General MIDI synthesiser behind `--music-device gm-builtin`: it plays the
game's music through a SoundFont the player supplies. From the
[FluidSynth](https://github.com/FluidSynth/fluidsynth) project, with the
[gcem](https://github.com/kthohr/gcem) headers its build needs. Vendored
source, unmodified but for one stub (below).

| | |
|---|---|
| upstream | https://github.com/FluidSynth/fluidsynth |
| tag | `v2.5.7` (release tarball, sha256 `ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a`) |
| licence | LGPL-2.1-or-later (`LICENSE`) |
| gcem | commit `012ae73c6d0a2cb09ffe86475f5c6fba3926e200` (FluidSynth's own pin, `cmake_admin/FindGCEM.cmake`), Apache-2.0 (`gcem/LICENSE`, `gcem/NOTICE.txt`); header-only, `gcem/include/` |
| local changes | **one**: `test/` is a single comment-only `CMakeLists.txt` (upstream adds the directory unconditionally, and its unit tests would register in our ctest run, unbuilt).  Otherwise files removed only: `doc/` except the inputs its CMake reads, `sf2/`, `contrib/` except `getopt/win/`, `test-android/`; gcem reduced to `include/` and its licence |
| `contrib/getopt/win/` | `getopt.c`, `getopt.h` (LGPL) from the git tag `v2.5.7`: the release tarball ships the directory EMPTY, and on Windows FluidSynth's CLI target (never built here, but configured) names them, so a Windows configure failed without them |

## Why vendored rather than `dlopen`'d

The same reasons as `../mt32emu/OLDUVAI-VENDORING.md`, one level worse: a
SoundFont played on Linux desktops only where the host had a
`libfluidsynth`, on macOS and Windows only through a copy the release
workflows built and bundled, and on the handhelds only where the firmware
happened to ship one. Compiled in, General MIDI works wherever a SoundFont
is present, one tested version everywhere.

Built with `osal=embedded` (FluidSynth's own OS layer instead of glib) and
every audio, MIDI, file and network driver off: the engine drives the synth
directly and does its own mixing, so nothing optional is reachable. The
result depends on nothing but the C and C++ runtimes and adds ~0.45 MB.

## Licence

LGPL-2.1-or-later, statically linked into a GPL-3.0-or-later program, as
libmt32emu is: LGPL-2.1-or-later upgrades to GPL-3.  The obligations we meet:
`LICENSE` ships in every package's licence folder, `THIRD-PARTY-NOTICES.md`
names the project and version, and the tag and tarball hash above identify
the corresponding source.  gcem (Apache-2.0) is header-only and compiled in:
its `LICENSE` and `NOTICE.txt` ship too.

**Keep this file accurate when re-syncing.** The tag and hash here are the
LGPL source pointer.

## Re-syncing

```sh
V=<new-version>
curl -fsSL https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v$V.tar.gz | tar xz -C /tmp
S=/tmp/fluidsynth-$V D=third_party/fluidsynth
rm -rf $D/src $D/include $D/cmake_admin $D/doc   # keep $D/test: our stub
cp -R $S/CMakeLists.txt $S/cmake_admin $S/src $S/include $S/LICENSE \
      $S/AUTHORS $S/README.md $S/THANKS $S/*.in $D/
mkdir -p $D/contrib/getopt/win && for f in getopt.c getopt.h; do
    curl -fsSL -o $D/contrib/getopt/win/$f \
        https://raw.githubusercontent.com/FluidSynth/fluidsynth/v$V/contrib/getopt/win/$f
done   # the tarball's copy of this directory is empty
mkdir -p $D/doc && cp -R $S/doc/CMakeLists.txt $S/doc/Doxyfile.cmake \
      $S/doc/doxygen $S/doc/fluidsettings.* $S/doc/fluidsynth.1 \
      $S/doc/examples $D/doc/
```

Then gcem at the revision `$D/cmake_admin/FindGCEM.cmake` names (its
`include/`, `LICENSE`, `NOTICE.txt` into `$D/gcem/`), update the table above,
and check `--music-device gm-builtin` still renders a SoundFont
(`scripts/metrics/audio_diff.sh`).
