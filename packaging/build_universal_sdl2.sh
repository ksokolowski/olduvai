#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Build a UNIVERSAL (arm64 + x86_64) SDL2 into a local prefix — Homebrew's
# bottle is single-arch, so the universal release dmg needs SDL2 from source.
# Version-pinned + checksum-verified; idempotent (skips if the prefix already
# holds the pinned version's universal dylib).
#   usage: packaging/build_universal_sdl2.sh <prefix>
set -eu
prefix="${1:?usage: build_universal_sdl2.sh <prefix>}"
SDL_VER="2.32.10"    # match the brew version the per-arch dev builds use
SDL_SHA256="5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
lib="${prefix}/lib/libSDL2-2.0.0.dylib"
if [ -f "${lib}" ] && lipo -archs "${lib}" | grep -q "x86_64 arm64" \
   && [ -f "${prefix}/.sdl2-${SDL_VER}" ]; then
    echo "universal SDL2 ${SDL_VER} already in ${prefix}"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT
curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-${SDL_VER}.tar.gz" \
     -o "${work}/sdl2.tar.gz"
echo "${SDL_SHA256}  ${work}/sdl2.tar.gz" | shasum -a 256 -c -
tar xzf "${work}/sdl2.tar.gz" -C "${work}"
cmake -S "${work}/SDL2-${SDL_VER}" -B "${work}/build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_INSTALL_PREFIX="${prefix}" -DSDL_TEST=OFF >/dev/null
cmake --build "${work}/build" -j >/dev/null
cmake --install "${work}/build" >/dev/null
touch "${prefix}/.sdl2-${SDL_VER}"
lipo -archs "${lib}" | xargs echo "universal SDL2 ${SDL_VER} ready:"
