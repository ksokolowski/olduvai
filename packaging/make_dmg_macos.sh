#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Package Olduvai.app into a compressed .dmg.
#
#   packaging/make_dmg_macos.sh              # native arch, brew SDL2 (dev/quick)
#   packaging/make_dmg_macos.sh --universal  # arm64+x86_64 fat binary — the
#                                          # RELEASE shape; builds a pinned
#                                          # universal SDL2 from source first
#                                          # (Homebrew bottles are single-arch)
#
# Output: ./olduvai-<version>-macos-<arch|universal>.dmg
set -eu
cd "$(dirname "$0")/.."
ver=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
command -v dylibbundler >/dev/null || {
    echo "need: brew install dylibbundler" >&2; exit 1; }

if [ "${1:-}" = "--universal" ]; then
    tag="universal"
    bdir="build/universal"
    prefix="$(pwd)/${bdir}/deps"
    sh packaging/build_universal_sdl2.sh "${prefix}"
    cmake -B "${bdir}" -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
          -DCMAKE_PREFIX_PATH="${prefix}" >/dev/null
    # -s: the from-source SDL2 has an @rpath install name dylibbundler can't
    # resolve on its own (brew's carries an absolute path).
    search="-s ${prefix}/lib"
else
    tag=$(uname -m)
    bdir="build/release"
    search=""
fi

# Force a fresh link of the app every run: dylibbundler rewrites install
# names in-place, so a bundle surviving from a previous run would feed the
# staging copy an already-rewritten binary (nothing left to bundle).
rm -rf "${bdir}/Olduvai.app"
cmake --build "${bdir}" --target olduvai_app -j

# Bundle a COPY in the staging dir — never mutate the build tree's app.
# (dylibbundler rewrites install names in-place; running it on the build
# output made second runs non-idempotent: the already-rewritten reference
# resolves to nothing, so nothing gets bundled.)
out="olduvai-${ver}-macos-${tag}.dmg"
stage="${bdir}/dmg-stage"
rm -rf "${stage}" && mkdir -p "${stage}/licenses"
cp -R "${bdir}/Olduvai.app" "${stage}/"
APP="${stage}/Olduvai.app"
bin="${APP}/Contents/MacOS/Olduvai"
# dylibbundler's exit code is unreliable on fat binaries (per-arch rpath
# rewrites can print errors yet converge) — run it best-effort and verify
# the OUTCOME strictly below; the checks are the contract, not the tool.
# shellcheck disable=SC2086  # ${search} is deliberately word-split
dylibbundler -od -b -x "${bin}" \
    -d "${APP}/Contents/libs/" -p "@executable_path/../libs/" ${search} \
    </dev/null || true

# ── hard verification of the bundle ──
otool -L "${bin}" | grep -q "@executable_path/../libs/libSDL2" \
    || { echo "make_dmg: SDL2 install name not rewritten" >&2; exit 1; }
if otool -L "${bin}" | grep -qE "/(opt/homebrew|usr/local)/|$(pwd)/"; then
    echo "make_dmg: absolute dylib path leaked into the binary:" >&2
    otool -L "${bin}" >&2; exit 1
fi
[ -f "${APP}/Contents/libs/libSDL2-2.0.0.dylib" ] \
    || { echo "make_dmg: bundled libSDL2 missing" >&2; exit 1; }
if [ "${tag}" = "universal" ]; then
    for f in "${bin}" "${APP}/Contents/libs/libSDL2-2.0.0.dylib"; do
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
cp third_party/rtmidi/LICENSE "${stage}/licenses/RtMidi-LICENSE.txt"
rm -f "${out}"
hdiutil create -volname "Olduvai" -srcfolder "${stage}" -ov -format UDZO "${out}"
echo "dmg ready: ${out}"
