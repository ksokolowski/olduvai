#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Package Olduvai.app into a compressed .dmg.
#
#   packaging/make_dmg_macos.sh              # native arch, brew SDL2 (dev/quick)
#   packaging/make_dmg_macos.sh --universal  # arm64+x86_64 fat binary — the
#                                          # RELEASE shape
#
# SDL2 is built from pinned source and linked STATICALLY, so the .app carries
# no SDL2 dylib and needs no dylibbundler.  The only staged library is
# FluidSynth, which is dlopen'd (and stays dynamic for LGPL §6 relinking).
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

# General MIDI: build a minimal, dependency-free FluidSynth to bundle.  It is
# dlopen'd, so neither dylibbundler (LOAD COMMANDS) nor ldd can see it — which
# is exactly how macOS shipped four releases with no GM support while
# developers, served by Homebrew, never noticed.  MT-32 needs no equivalent:
# libmt32emu is vendored and compiled in (third_party/mt32emu).
sh packaging/build_fluidsynth.sh "${prefix}"

# Fresh link every run: the .app is a build artefact, and a surviving one
# could carry a stale binary or a stale Contents/libs from an earlier flavour.
rm -rf "${bdir}/Olduvai.app"
cmake --build "${bdir}" --target olduvai_app -j

# Stage a COPY — never mutate the build tree's app.  Staging adds FluidSynth
# and re-signs, and doing that in place would make the build tree's .app
# differ from what a plain `cmake --build --target olduvai_app` produces.
out="olduvai-${ver}-macos-${tag}.dmg"
stage="${bdir}/dmg-stage"
rm -rf "${stage}" && mkdir -p "${stage}/licenses"
cp -R "${bdir}/Olduvai.app" "${stage}/"
APP="${stage}/Olduvai.app"
bin="${APP}/Contents/MacOS/Olduvai"

# NO dylibbundler.  SDL2 is linked IN (OLDUVAI_STATIC_SDL), libmt32emu is
# vendored, and FluidSynth is dlopen'd — which dylibbundler could never see
# anyway, since it walks LOAD COMMANDS.  So nothing is left for it to bundle,
# and dropping it removes the macOS 26 failure it caused: its install-name
# rewriting produced a binary that SIGKILLs without re-signing (-ns, rc=137)
# and HANGS in signature assessment with it (rc=124, 38 minutes before being
# killed).  Isolated on a pristine bundle with no FluidSynth involved: before
# dylibbundler the app runs, after it does not.
#
# The only thing still staged is FluidSynth, under exactly the unversioned
# name audio.cpp's load_fluidsynth() asks for.  A versioned copy would ride
# along as dead weight while a dev machine, served by Homebrew, still looked
# fine — the silent-success trap that hid the original packaging bug.
mkdir -p "${APP}/Contents/libs"
cp "${prefix}/lib/libfluidsynth.dylib" "${APP}/Contents/libs/libfluidsynth.dylib"
# Adding a file to a bundle invalidates its seal, and an invalid signature
# makes dlopen SIGKILL rather than fail — turning graceful OPL fallback into a
# crash.  Re-seal after staging.
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

# Assert the ARTIFACT, not the build host.  The Linux lane's equivalent
# checks `ldconfig -p` — i.e. that the library exists on the machine doing
# the packaging — and then prints a manifest that always exits 0, so a
# packaging tool that silently dropped a library would still produce a green
# release.  Check what is actually in the bundle, under the exact name the
# loader will ask for, and prove the shipped binary can really load it.
[ -f "${APP}/Contents/libs/libfluidsynth.dylib" ] \
    || { echo "make_dmg: bundled libfluidsynth missing" >&2; exit 1; }
if otool -L "${APP}/Contents/libs/libfluidsynth.dylib" | tail -n +2 \
     | grep -qE "/(opt/homebrew|usr/local)/"; then
    echo "make_dmg: bundled libfluidsynth pulls third-party dylibs" >&2
    otool -L "${APP}/Contents/libs/libfluidsynth.dylib" >&2; exit 1
fi
# An invalid signature makes dlopen SIGKILL rather than fail, which would
# turn the engine's graceful OPL fallback into a crash on a user's machine.
codesign --verify "${APP}/Contents/libs/libfluidsynth.dylib" 2>/dev/null \
    || { echo "make_dmg: bundled libfluidsynth signature is invalid — dlopen would SIGKILL" >&2; exit 1; }
# Strongest available check short of shipping a test binary: actually dlopen
# it and resolve a symbol the engine binds.  This is the assertion that would
# have caught every failure mode found while writing this — a library the
# loader cannot find, cannot open, or is killed for opening.  (An invalid
# signature SIGKILLs the *caller*, so a crash here is a real failure, not a
# flake.)  Skipped, loudly, if python3 is absent rather than passing quietly.
if command -v python3 >/dev/null 2>&1; then
    python3 -c "import ctypes,sys; ctypes.CDLL(sys.argv[1]).new_fluid_settings" \
        "${APP}/Contents/libs/libfluidsynth.dylib" \
        || { echo "make_dmg: bundled libfluidsynth will not dlopen" >&2; exit 1; }
else
    echo "make_dmg: NOTE — python3 absent, skipped the dlopen check" >&2
fi
if otool -L "${bin}" | grep -qE "/(opt/homebrew|usr/local)/|$(pwd)/"; then
    echo "make_dmg: absolute dylib path leaked into the binary:" >&2
    otool -L "${bin}" >&2; exit 1
fi
if [ "${tag}" = "universal" ]; then
    for f in "${bin}" "${APP}/Contents/libs/libfluidsynth.dylib"; do
        lipo -archs "${f}" | grep -q "x86_64 arm64" \
            || { echo "make_dmg: ${f} is not universal" >&2; exit 1; }
    done
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
rm -f "${out}"
hdiutil create -volname "Olduvai" -srcfolder "${stage}" -ov -format UDZO "${out}"
echo "dmg ready: ${out}"
