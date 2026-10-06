#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Package Olduvai.app into a compressed .dmg.
#
#   packaging/make_dmg_macos.sh              # native arch, brew SDL2 (dev/quick)
#   packaging/make_dmg_macos.sh --universal  # arm64+x86_64 fat binary — the
#                                          # RELEASE shape
#
# SDL2 is built from pinned source and linked STATICALLY; libmt32emu and
# FluidSynth are vendored and compiled in.  So the .app carries no library
# at all and needs no dylibbundler.
#
# Output: ./olduvai-<version>-macos-<arch|universal>.dmg
set -eu
cd "$(dirname "$0")/.."
ver=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)

if [ "${1:-}" = "--universal" ]; then
    tag="universal"
    bdir="build/universal"
    prefix="$(pwd)/${bdir}/deps"
    sh packaging/build_universal_sdl2.sh "${prefix}"
    cmake -B "${bdir}" -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
          -DOLDUVAI_STATIC_SDL=ON \
          -DSDL2_DIR="${prefix}/lib/cmake/SDL2" \
          -DCMAKE_PREFIX_PATH="${prefix}" >/dev/null
else
    tag=$(uname -m)
    bdir="build/release"
    prefix="$(pwd)/${bdir}/deps"
    sh packaging/build_universal_sdl2.sh "${prefix}"
    # -DSDL2_DIR explicitly: a cached SDL2_DIR from an earlier configure wins
    # over CMAKE_PREFIX_PATH, and the build then silently falls back to the
    # system's SHARED SDL2 — caught by the no-third-party-dylib assertion
    # below, which is exactly what it is for.
    cmake -B "${bdir}" -DCMAKE_BUILD_TYPE=Release -DOLDUVAI_STATIC_SDL=ON \
          -DSDL2_DIR="${prefix}/lib/cmake/SDL2" \
          -DCMAKE_PREFIX_PATH="${prefix}" >/dev/null
fi

# Fresh link every run: the .app is a build artefact, and a surviving one
# could carry a stale binary or a stale Contents/libs from an earlier flavour.
rm -rf "${bdir}/Olduvai.app"
cmake --build "${bdir}" --target olduvai_app -j

# Stage a COPY — never mutate the build tree's app: it must stay what a
# plain `cmake --build --target olduvai_app` produces.
out="olduvai-${ver}-macos-${tag}.dmg"
stage="${bdir}/dmg-stage"
rm -rf "${stage}" && mkdir -p "${stage}/licenses"
cp -R "${bdir}/Olduvai.app" "${stage}/"
APP="${stage}/Olduvai.app"
bin="${APP}/Contents/MacOS/Olduvai"

# NO dylibbundler.  SDL2 is linked IN (OLDUVAI_STATIC_SDL), libmt32emu and
# FluidSynth are vendored, so nothing is left for it to bundle; and its
# install-name rewriting produced a binary that SIGKILLs on macOS 26 without
# re-signing (-ns, rc=137) and HANGS in signature assessment with it.
# Ad-hoc seal of the staged bundle (the release is not notarised).
codesign --force --deep --sign - "${APP}" >/dev/null 2>&1 || true

# ── hard verification of the bundle ──
# Stronger than the old "SDL2 install name was rewritten" check: assert the
# binary needs NO third-party dylib at all.  (otool prints a per-architecture
# header line for a fat binary that a thin one does not — match only indented
# dependency lines, or the header counts as a dependency.)
if otool -L "${bin}" | grep -E "^\s" \
     | grep -vE "/usr/lib/|/System/Library/" | grep -q .; then
    echo "make_dmg: the binary still needs a third-party dylib:" >&2
    otool -L "${bin}" | grep -E "^\s" | grep -vE "/usr/lib/|/System/Library/" >&2
    exit 1
fi

# Nor any library staged beside it.
if [ -d "${APP}/Contents/libs" ] && [ -n "$(ls -A "${APP}/Contents/libs")" ]; then
    echo "make_dmg: the bundle carries libraries in Contents/libs:" >&2
    ls "${APP}/Contents/libs" >&2; exit 1
fi
if otool -L "${bin}" | grep -qE "/(opt/homebrew|usr/local)/|$(pwd)/"; then
    echo "make_dmg: absolute dylib path leaked into the binary:" >&2
    otool -L "${bin}" >&2; exit 1
fi
if [ "${tag}" = "universal" ]; then
    lipo -archs "${bin}" | grep -q "x86_64 arm64" \
        || { echo "make_dmg: ${bin} is not universal" >&2; exit 1; }
fi
"${bin}" --version >/dev/null \
    || { echo "make_dmg: bundled binary failed to run" >&2; exit 1; }

ln -s /Applications "${stage}/Applications"
cp LICENSE "${stage}/LICENSE.txt"
# Third-party license texts (binary distribution obligation — see
# THIRD-PARTY-NOTICES.md; Nuked-OPL3 is LGPL-2.1 compiled in).
cp THIRD-PARTY-NOTICES.md "${stage}/licenses/"
cp third_party/nuked_opl3/LICENSE "${stage}/licenses/Nuked-OPL3-LICENSE.txt"
# libmt32emu is vendored and statically linked (LGPL-2.1 §3 conversion to
# GPL, same as Nuked-OPL3) — its text must ship with the binary.
cp third_party/mt32emu/COPYING.LESSER.txt \
   "${stage}/licenses/libmt32emu-LICENSE.txt"
cp third_party/rtmidi/LICENSE "${stage}/licenses/RtMidi-LICENSE.txt"
# FluidSynth (LGPL-2.1) and the gcem headers it builds with (Apache-2.0),
# vendored and compiled in (third_party/fluidsynth/OLDUVAI-VENDORING.md).
cp third_party/fluidsynth/LICENSE "${stage}/licenses/FluidSynth-LICENSE.txt"
cp third_party/fluidsynth/gcem/LICENSE "${stage}/licenses/gcem-LICENSE.txt"
cp third_party/fluidsynth/gcem/NOTICE.txt "${stage}/licenses/gcem-NOTICE.txt"
rm -f "${out}"
hdiutil create -volname "Olduvai" -srcfolder "${stage}" -ov -format UDZO "${out}"
echo "dmg ready: ${out}"
