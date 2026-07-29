#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Build a UNIVERSAL (arm64 + x86_64) STATIC SDL2 into a local prefix —
# Homebrew's bottle is single-arch, so the universal release dmg needs SDL2
# from source, and it is linked IN rather than bundled.
#
# Static because SDL2 was the last third-party dylib in the .app, and removing
# it removes dylibbundler — whose install-name rewriting yields a binary that
# SIGKILLs (-ns) or hangs (its own re-signing) on macOS 26.  Verified: the
# resulting bundle has no Contents/libs, runs where the bundled one hangs, and
# produces byte-identical audio and pixels.  zlib licence, so no relinking
# obligation.
# Version-pinned + checksum-verified; idempotent (skips if the prefix already
# holds the pinned version's universal dylib).
#   usage: packaging/build_universal_sdl2.sh <prefix>
set -eu
prefix="${1:?usage: build_universal_sdl2.sh <prefix>}"
SDL_VER="2.32.10"    # match the brew version the per-arch dev builds use
SDL_SHA256="5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
lib="${prefix}/lib/libSDL2.a"
if [ -f "${lib}" ] && lipo -archs "${lib}" | grep -q "x86_64 arm64" \
   && [ -f "${prefix}/.sdl2-${SDL_VER}" ]; then
    echo "universal static SDL2 ${SDL_VER} already in ${prefix}"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT
curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-${SDL_VER}.tar.gz" \
     -o "${work}/sdl2.tar.gz"
echo "${SDL_SHA256}  ${work}/sdl2.tar.gz" | shasum -a 256 -c -
tar xzf "${work}/sdl2.tar.gz" -C "${work}"
# -w: vendored code is not held to our warning bar (CMakeLists.txt,
# olduvai_silence_vendored_target).  SDL2's ObjC joystick backend alone
# accounted for 50 of the 193 compiler warnings in the 2026-07-26 dry run —
# all -Wdeprecated-declarations against macOS APIs, all upstream's to fix.
# Drowning our own warnings in them is the actual cost.
# -w via CFLAGS/CXXFLAGS, NOT -DCMAKE_C_FLAGS: the -D form REPLACES
# CMAKE_C_FLAGS_INIT, which on some platforms carries load-bearing
# platform defines.  On Windows that exact mistake disarms FluidSynth's
# export macro (see build_fluidsynth_windows.cmd for the full autopsy);
# these two are POSIX-only, but the same lever is used here so the three
# scripts do not disagree about how to silence a vendored build.
export CFLAGS="${CFLAGS:-} -w"
export CXXFLAGS="${CXXFLAGS:-} -w"
export OBJCFLAGS="${OBJCFLAGS:-} -w"
cmake -S "${work}/SDL2-${SDL_VER}" -B "${work}/build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_INSTALL_PREFIX="${prefix}" -DSDL_TEST=OFF \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON >/dev/null
cmake --build "${work}/build" -j >/dev/null
cmake --install "${work}/build" >/dev/null
touch "${prefix}/.sdl2-${SDL_VER}"
lipo -archs "${lib}" | xargs echo "universal STATIC SDL2 ${SDL_VER} ready:"
