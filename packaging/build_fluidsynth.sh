#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Build a MINIMAL, dependency-free FluidSynth into a local prefix.
#   usage: packaging/build_fluidsynth.sh <prefix>
#
# WHY FROM SOURCE.  General MIDI shipped on Linux only; macOS and Windows
# users silently fell back to OPL.  The obvious fix — bundle the system
# library — is a trap on macOS: Homebrew's libfluidsynth drags a FIFTEEN
# dylib, 5.9 MB transitive closure (glib, gthread, gettext, libsndfile,
# portaudio, readline, pcre2 …) and is arm64-only, while the release dmg is
# universal and asserts every bundled dylib is fat.
#
# That closure is Homebrew's MAXIMAL configuration, not FluidSynth.  Measured
# on 2.5.7:
#
#   Homebrew build                         15 dylibs   5.9 MB
#   minimal build, default (glib) OSAL      4 dylibs   2.2 MB
#   minimal build, -Dosal=embedded          0 dylibs   0.6 MB
#
# -Dosal=embedded selects FluidSynth's own OS-abstraction layer instead of
# glib's, which removes the last third-party dependency entirely.  ScummVM
# uses the same switch for the same reason.  Verified here end to end: the
# resulting dylib loads through our dlopen path, parses a real SoundFont and
# renders audible PCM through the shipping --render-audio CLI.
#
# Version-pinned + checksum-verified (same bar as build_universal_sdl2.sh) and
# idempotent.  On macOS it builds universal, because the release dmg is fat.
set -eu
prefix="${1:?usage: build_fluidsynth.sh <prefix>}"

FS_VER="2.5.7"
# GitHub's auto-generated tag archives are not contractually byte-stable the
# way a release asset is, so a mismatch here may mean "GitHub re-rolled the
# tarball", not "someone tampered with it".  Verify against upstream before
# assuming the worst — but never skip the check.
FS_SHA256="ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a"

case "$(uname -s)" in
    Darwin) libname="libfluidsynth.dylib" ;;
    *)      libname="libfluidsynth.so" ;;
esac
lib="${prefix}/lib/${libname}"

if [ -f "${lib}" ] && [ -f "${prefix}/.fluidsynth-${FS_VER}" ]; then
    echo "minimal FluidSynth ${FS_VER} already in ${prefix}"
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT
curl -fsSL \
  "https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v${FS_VER}.tar.gz" \
  -o "${work}/fluidsynth.tar.gz"
if command -v sha256sum >/dev/null 2>&1; then
    echo "${FS_SHA256}  ${work}/fluidsynth.tar.gz" | sha256sum -c -
else
    echo "${FS_SHA256}  ${work}/fluidsynth.tar.gz" | shasum -a 256 -c -
fi
tar xzf "${work}/fluidsynth.tar.gz" -C "${work}"

# Everything optional OFF.  Each of these is a third-party dependency we would
# otherwise have to bundle, sign and license; none is reachable through the
# small C-API subset the engine binds (settings/synth/sfload/noteon/noteoff/
# cc/program_change/pitch_bend/write_s16).  We drive the synth directly and do
# our own mixing and resampling, so FluidSynth needs no audio or MIDI driver
# of its own.
arch_flag=""
[ "$(uname -s)" = "Darwin" ] && arch_flag="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"

# shellcheck disable=SC2086  # arch_flag is deliberately unquoted/word-split
# -w: vendored code is not held to our warning bar (CMakeLists.txt,
# olduvai_silence_vendored_target).  Nine warnings in the 2026-07-26 dry run,
# all MSVC narrowing/enum-mix in fluid_dls.cpp and fluid_synth.c — upstream's
# to fix, and we do not patch this tree for the same reason we do not patch
# mt32emu's.
# -w via CFLAGS/CXXFLAGS, NOT -DCMAKE_C_FLAGS: the -D form REPLACES
# CMAKE_C_FLAGS_INIT, which on some platforms carries load-bearing
# platform defines.  On Windows that exact mistake disarms FluidSynth's
# export macro (see build_fluidsynth_windows.cmd for the full autopsy);
# these two are POSIX-only, but the same lever is used here so the three
# scripts do not disagree about how to silence a vendored build.
export CFLAGS="${CFLAGS:-} -w"
export CXXFLAGS="${CXXFLAGS:-} -w"
cmake -S "${work}/fluidsynth-${FS_VER}" -B "${work}/build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${prefix}" \
      ${arch_flag} \
      -Dosal=embedded \
      -DBUILD_SHARED_LIBS=ON \
      -Denable-framework=OFF \
      -Denable-libsndfile=OFF -Denable-readline=OFF -Denable-network=OFF \
      -Denable-aufile=OFF -Denable-portaudio=OFF -Denable-jack=OFF \
      -Denable-pulseaudio=OFF -Denable-alsa=OFF -Denable-oss=OFF \
      -Denable-dbus=OFF -Denable-libinstpatch=OFF -Denable-ipv6=OFF \
      -Denable-openmp=OFF -Denable-sdl3=OFF \
      -Denable-coreaudio=OFF -Denable-coremidi=OFF -Denable-dsound=OFF \
      -Denable-wasapi=OFF -Denable-waveout=OFF -Denable-winmidi=OFF \
      >/dev/null
cmake --build "${work}/build" -j >/dev/null
cmake --install "${work}/build" >/dev/null

# The engine dlopens an UNVERSIONED name (audio.cpp load_fluidsynth), while
# the install lays down libfluidsynth.3.x.y with symlinks.  Stage the real
# file under the exact name asked for: a versioned copy would sit in the
# package as dead weight while a dev machine, served by Homebrew, still looks
# fine — the same silent-success trap that hid the original bug.
real="$(cd "${prefix}/lib" && ls libfluidsynth*.dylib libfluidsynth*.so.* 2>/dev/null \
        | grep -vE "^${libname}$" | head -1 || true)"
if [ -n "${real}" ]; then
    rm -f "${lib}"
    cp "${prefix}/lib/${real}" "${lib}"
fi

if [ "$(uname -s)" = "Darwin" ]; then
    # Point the id at where it will actually live.  install_name_tool
    # invalidates the signature the linker applied, and on arm64 an
    # invalid signature makes dlopen SIGKILL the process rather than fail —
    # which would turn our graceful OPL fallback into a crash.  Re-sign.
    install_name_tool -id "@executable_path/../libs/${libname}" "${lib}"
    codesign --force --sign - "${lib}" 2>/dev/null || true
    lipo -archs "${lib}" | grep -q "x86_64 arm64" \
        || { echo "build_fluidsynth: ${lib} is not universal" >&2; exit 1; }
fi

# Assert the point of the whole exercise: no third-party dependencies.
if [ "$(uname -s)" = "Darwin" ]; then
    if otool -L "${lib}" | tail -n +2 \
         | grep -qE "/(opt/homebrew|usr/local)/"; then
        echo "build_fluidsynth: FAIL — bundled FluidSynth still has third-party deps:" >&2
        otool -L "${lib}" >&2
        exit 1
    fi
fi

touch "${prefix}/.fluidsynth-${FS_VER}"
echo "minimal FluidSynth ${FS_VER} ready: ${lib}"
