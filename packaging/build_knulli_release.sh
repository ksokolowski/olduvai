#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The two KNULLI release zips, built on the Mac with zig — one command for the
# release cut (docs/internal/RELEASE_CUT_RUNBOOK.md, "Handheld bundles").
#
#   olduvai-<version>-knulli-trimui.zip   TrimUI Smart Pro   aarch64
#   olduvai-<version>-knulli-a12.zip      Powkiddy A12       armhf
#
# Each: cross-build from --src (RUN IT ON THE EXPORT TREE, so the build ID is
# the public commit's), strip, build_port_knulli.sh --public, zip.  Prints the
# SHA-256 lines to append to the release's SHA256SUMS.txt.
#
# Needs zig and the per-arch sysroot + wrapper tools the zig toolchain files
# expect (cmake/toolchains/{aarch64,armhf}-zig.cmake; set up once per machine,
# see the A12 runbook): $OLDUVAI_CROSS, default ~/olduvai_cross, holding
# sysroot/ + xtool/ (aarch64) and sysroot-armhf/ + xtool-armhf/.
set -euo pipefail

SRC="" OUT=""
CROSS="${OLDUVAI_CROSS:-$HOME/olduvai_cross}"
while [ $# -gt 0 ]; do
    case "$1" in
        --src) SRC="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        -h|--help) sed -n '4,17p' "$0" | sed 's/^# \{0,1\}//'
                   echo; echo "usage: $0 --src TREE --out DIR"; exit 0 ;;
        *) echo "build_knulli_release: unknown option '$1'" >&2; exit 2 ;;
    esac
done
[ -n "$SRC" ] && [ -n "$OUT" ] || {
    echo "build_knulli_release: --src and --out are required" >&2; exit 2; }
SRC=$(cd "$SRC" && pwd -P)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd -P)
case "${OUT}/" in "${SRC}/"*)
    echo "build_knulli_release: --out must be outside the source tree" >&2; exit 1 ;;
esac
STRIP="${LLVM_STRIP:-$(command -v llvm-strip || echo /opt/homebrew/opt/llvm/bin/llvm-strip)}"
[ -x "$STRIP" ] || { echo "build_knulli_release: needs llvm-strip (brew install llvm)" >&2; exit 2; }
VER=$(awk '/^project\(olduvai/ {p = 1} p && $1 == "VERSION" {print $2; exit}' \
          "$SRC/CMakeLists.txt")
[ -n "$VER" ] || { echo "build_knulli_release: no VERSION in $SRC/CMakeLists.txt" >&2; exit 1; }

build() {   # device arch sysroot xtool
    local dev="$1" arch="$2" sysroot="$CROSS/$3" xtool="$CROSS/$4"
    local b="$OUT/build-$arch"
    [ -d "$sysroot" ] && [ -d "$xtool" ] || {
        echo "build_knulli_release: missing $sysroot or $xtool" >&2; exit 2; }
    cmake -S "$SRC" -B "$b" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$SRC/cmake/toolchains/$arch-zig.cmake" \
        -DOLDUVAI_ZIG_SYSROOT="$sysroot" -DOLDUVAI_ZIG_XTOOL="$xtool" \
        -DSDL2_DIR="$sysroot/usr/lib/cmake/SDL2" >/dev/null
    cmake --build "$b" --target olduvai -j8 >/dev/null
    "$STRIP" -o "$OUT/olduvai.$arch" "$b/olduvai"
    rm -rf "${OUT:?}/${dev:?}"
    bash "$SRC/packaging/build_port_knulli.sh" --public --device "$dev" \
        --binary "$OUT/olduvai.$arch" --out "$OUT/$dev" >/dev/null
    local zip="olduvai-${VER}-knulli-${dev}.zip"
    rm -f "$OUT/$zip"
    (cd "$OUT/$dev" && zip -qrX "../$zip" .)
    echo "  $zip  ($(wc -c < "$OUT/$zip" | tr -d ' ') bytes)"
}
echo "Olduvai ${VER} KNULLI bundles from ${SRC}:"
build trimui aarch64 sysroot       xtool
build a12    armhf   sysroot-armhf xtool-armhf
# No `grep -m1` / `head` here: under pipefail, closing the pipe early kills
# `strings` with SIGPIPE and the script with it — silently, after the zips.
# A dirty tree reads "<hash>-dirty, built <time>" — and a release must not be
# one, so say so loudly.  Informational either way: never fail the script here.
bid=$(strings "$OUT/olduvai.aarch64" |
      grep -E "^[0-9a-f]{7}(-dirty)?, (built )?[0-9]{4}-" | sed -n 1p || true)
echo "  build ID: ${bid:-<not found>}"
case "$bid" in *-dirty*)
    echo "  WARNING: built from uncommitted changes — not a release build." >&2 ;;
esac
echo "SHA-256 (append to SHA256SUMS.txt):"
(cd "$OUT" && shasum -a 256 "olduvai-${VER}-knulli-"*.zip)
