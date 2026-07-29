# libmt32emu — vendored

Roland MT-32 / CM-32L emulation, from the [Munt](https://github.com/munt/munt)
project. Vendored source, unmodified.

| | |
|---|---|
| upstream | https://github.com/munt/munt |
| tag | `libmt32emu_2_8_3` |
| commit | `3b05ec276f9e605af86b0eaef7f5eda43477a31f` |
| licence | LGPL-2.1-or-later (`COPYING.LESSER.txt`) |
| local changes | **none** — `src/test/` removed, nothing else |

## Why vendored rather than `dlopen`'d

It used to be loaded at runtime, and that shipped a real defect for four
public releases: the library reached Linux AppImage users only. macOS and
Windows builds carried nothing, so those users silently fell back to the
vendored OPL/AdLib emulation. Music still played, so nobody reported it — a
capability loss with no symptom.

Every attempt to fix that by *packaging* ran into a different hazard on each
platform: a leaf-name `dlopen` cannot see inside a macOS `.app`; rewriting a
Homebrew dylib's install name invalidates its ad-hoc signature and turns the
next `dlopen` into a SIGKILL rather than a graceful fallback; `dylibbundler
-od` erases the directory you staged libraries into; Munt publishes no
Windows DLL at all; and the loader asks for unversioned filenames while every
real artifact is versioned. Four platforms' worth of ways to ship a package
whose library the loader never finds — while a developer's machine, served by
a hardcoded Homebrew path, looks perfectly fine.

Compiled in, none of that exists. This is also what ScummVM does, and has done
for over a decade: it carries the same Munt source in `audio/softsynth/mt32/`,
builds it by default, and depends on no external libmt32emu at all.

The cost is what ScummVM pays too: ~1 MB of third-party source in the tree and
a re-sync every year or two.

## Licence

LGPL-2.1-or-later, statically linked into a GPL-3.0-or-later program. That
combination is fine — LGPL-2.1-or-later upgrades to GPL-3 — and it is exactly
what ScummVM does. The obligations we meet: `COPYING.LESSER.txt` ships in the
package licence folder, `THIRD-PARTY-NOTICES.md` names the project and
version, and the tag and commit above identify the corresponding source.

**Keep this file accurate when re-syncing.** The tag and commit here are the
LGPL source pointer — if they drift from what is actually in this directory,
the licence obligation is not met.

## Re-syncing

```sh
git clone --depth 1 --branch <new-tag> https://github.com/munt/munt /tmp/munt
rm -rf third_party/mt32emu/src third_party/mt32emu/cmake
cp -R /tmp/munt/mt32emu/src /tmp/munt/mt32emu/cmake third_party/mt32emu/
rm -rf third_party/mt32emu/src/test
cp /tmp/munt/mt32emu/{CMakeLists.txt,COPYING*.txt,AUTHORS.txt,README.md} \
   third_party/mt32emu/
```

Then update the table above, run `ctest --preset release -R audio_render`, and
check `--music-device mt32-builtin` still renders non-silent PCM.
