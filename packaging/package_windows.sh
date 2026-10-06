#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Stage the portable Windows release zip.  The default Windows build shape
# (OLDUVAI_STATIC_RUNTIME + OLDUVAI_STATIC_SDL, CMakeLists) makes olduvai.exe
# self-contained — no DLLs to deploy.  Zip layout: olduvai/ top-level dir with
# the exe, the OFL HD fonts (resolver searches exe_dir/fonts/), licences and a
# run-me readme.  No game content, ever.
#   usage: [BUILD_DIR=build/release] packaging/package_windows.sh
set -eu
cd "$(dirname "$0")/.."
BUILD_DIR="${BUILD_DIR:-build/release}"
ver=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
out="olduvai-${ver}-windows-x86_64.zip"
stage="${BUILD_DIR}/zip-stage"
rm -rf "${stage}" && mkdir -p "${stage}/olduvai/fonts"
cp "${BUILD_DIR}/olduvai.exe" "${stage}/olduvai/"
# MSVC builds ship SDL2.dll beside the exe (CMake copies it there);
# the static MinGW build has none — copy any that exist.
for dll in "${BUILD_DIR}"/*.dll; do
    [ -e "${dll}" ] && cp "${dll}" "${stage}/olduvai/"
done

# FluidSynth is compiled in (third_party/fluidsynth): a libfluidsynth DLL
# left in the build directory by an older build must not ride along.
rm -f "${stage}"/olduvai/libfluidsynth*.dll
cp "${BUILD_DIR}"/fonts/*.ttf "${BUILD_DIR}"/fonts/*LICENSE* "${stage}/olduvai/fonts/"
# Menu model beside the exe (searched first at runtime; the embedded copy
# is only the lone-binary fallback — shipping the file keeps it user-
mkdir -p "${stage}/olduvai/data"
cp LICENSE "${stage}/olduvai/LICENSE.txt"
# Third-party license texts (binary distribution obligation — see
# THIRD-PARTY-NOTICES.md; Nuked-OPL3 is LGPL-2.1 compiled in, SDL2 static).
mkdir -p "${stage}/olduvai/licenses"
cp THIRD-PARTY-NOTICES.md "${stage}/olduvai/licenses/"
cp third_party/nuked_opl3/LICENSE "${stage}/olduvai/licenses/Nuked-OPL3-LICENSE.txt"
# libmt32emu is vendored and statically linked (LGPL-2.1 §3 conversion to
# GPL, same as Nuked-OPL3) — its text must ship with the binary.
cp third_party/mt32emu/COPYING.LESSER.txt \
   "${stage}/olduvai/licenses/libmt32emu-LICENSE.txt"
cp third_party/rtmidi/LICENSE "${stage}/olduvai/licenses/RtMidi-LICENSE.txt"
# FluidSynth (LGPL-2.1) and the gcem headers it builds with (Apache-2.0),
# vendored and compiled in (third_party/fluidsynth/OLDUVAI-VENDORING.md).
cp third_party/fluidsynth/LICENSE "${stage}/olduvai/licenses/FluidSynth-LICENSE.txt"
cp third_party/fluidsynth/gcem/LICENSE "${stage}/olduvai/licenses/gcem-LICENSE.txt"
cp third_party/fluidsynth/gcem/NOTICE.txt "${stage}/olduvai/licenses/gcem-NOTICE.txt"
cat > "${stage}/olduvai/README-windows.txt" <<'EOF'
Olduvai - portable Windows build
================================

Olduvai is an engine only: it ships NO game content.  You need your own
copy of Prehistorik (1991) - e.g. "Prehistorik 1+2" on GOG.com.

Run:
    olduvai.exe --game-dir C:\path\to\prehistorik\files --play
A GOG installation is auto-discovered, so with a GOG copy a plain
    olduvai.exe --play
works.  Settings live in %APPDATA%\olduvai\play.json.

Windows SmartScreen: this build is not code-signed, so the first launch
may show "Windows protected your PC".  Click "More info", then
"Run anyway".  You can verify your download against SHA256SUMS.txt on
the release page:  certutil -hashfile olduvai.exe SHA256

Third-party components and licenses: see the licenses\ folder.
Project page, documentation and licence: see LICENSE.txt and the
repository README.
EOF
rm -f "${out}"
if command -v zip >/dev/null 2>&1; then
    (cd "${stage}" && zip -qr - olduvai) > "${out}"
else
    # No zip on the box (bare mingw hosts / git-bash) — PowerShell ships on
    # every Windows machine; Compress-Archive produces the same layout.
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$(cygpath -w "${stage}/olduvai")' \
         -DestinationPath '$(cygpath -w "$(pwd)/${out}")' -Force"
fi
echo "zip ready: ${out}"
