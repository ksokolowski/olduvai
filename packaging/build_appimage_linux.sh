#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Build a self-contained olduvai-x86_64.AppImage.
#
# Ships NO game content — the user supplies game files at runtime via
# --game-dir / config / GOG auto-discovery.  Bundles: SDL2 (+ transitive deps,
# ldd-driven), libfluidsynth (dlopen'd → injected explicitly, invisible to
# ldd), and both OFL HD fonts.  libasound and any SoundFont stay host-provided.
#
# glibc floor: the AppImage runs only on distros whose glibc >= this build
# host's.  For broad portability build on an old-baseline system/container
# (follow-up); on a bleeding-edge host it runs on bleeding-edge distros only.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"   # repo root
build_dir="${here}/build/appimage"
appdir="${build_dir}/AppDir"
tools_dir="${build_dir}/tools"
export APPIMAGE_EXTRACT_AND_RUN=1   # run the tool AppImages without FUSE

# Tagged upstream releases, checksum-asserted before execution (release
# path: never run a mutable 'continuous' binary — same pattern as the
# pinned SDL2 downloads).  Bump tag + sha together.
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

fetch_tool() {   # $1=url  $2=dest  $3=sha256
    if [[ ! -x "$2" ]]; then
        echo ">> fetching $(basename "$2")"
        mkdir -p "$(dirname "$2")"
        curl -fL "$1" -o "$2"
        echo "$3  $2" | sha256sum -c - || {
            echo "!! checksum mismatch for $(basename "$2") — refusing to run it" >&2
            rm -f "$2"
            exit 1
        }
        chmod +x "$2"
    fi
}

# 1. Configure + build Release (hardening + LTO on by default).
cmake -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release "${here}"
cmake --build "${build_dir}" -j"$(nproc)" --target olduvai

# 2. Stage the AppDir.
rm -rf "${appdir}"
install -Dm755 "${build_dir}/olduvai" "${appdir}/usr/bin/olduvai"

# HD fonts (both OFL faces + licences) beside the binary — the resolver
# (src/enhance/hd_text.cpp) searches exe_dir/fonts/.
install -d "${appdir}/usr/bin/fonts"
cp "${build_dir}/fonts/"*.ttf "${build_dir}/fonts/"*LICENSE* "${appdir}/usr/bin/fonts/"

# Menu model beside the binary (searched first at runtime; embedded copy is
# the lone-binary fallback — shipping keeps it user-customisable).
install -d "${appdir}/usr/bin/data"
cp "${build_dir}/data/menus.json" "${appdir}/usr/bin/data/"

# License texts (binary distribution obligation — GPLv3 §6 for the
# project's own license, THIRD-PARTY-NOTICES.md for the rest; Nuked-OPL3
# LGPL-2.1 compiled in, FluidSynth LGPL-2.1 bundled as a replaceable .so).
install -d "${appdir}/usr/bin/licenses"
cp "${here}/LICENSE" "${appdir}/usr/bin/licenses/LICENSE.txt"
cp "${here}/THIRD-PARTY-NOTICES.md" "${appdir}/usr/bin/licenses/"
cp "${here}/third_party/nuked_opl3/LICENSE" "${appdir}/usr/bin/licenses/Nuked-OPL3-LICENSE.txt"
cp "${here}/third_party/rtmidi/LICENSE" "${appdir}/usr/bin/licenses/RtMidi-LICENSE.txt"

# Desktop entry + icon (the app's own icon, not game content).
cat > "${build_dir}/olduvai.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Olduvai
Comment=Native recreation of the Prehistorik (1991) engine
Exec=olduvai
Icon=olduvai
Categories=Game;
Terminal=false
EOF
# IM7 has `magick`; ubuntu-22.04's apt ImageMagick is IM6 (`convert` only).
if command -v magick >/dev/null 2>&1; then
    magick "${here}/assets/icon/icon_src.png" -resize 256x256 "${build_dir}/olduvai.png"
else
    convert "${here}/assets/icon/icon_src.png" -resize 256x256 "${build_dir}/olduvai.png"
fi

# 3. Populate deps with linuxdeploy: SDL2 (+ transitive) via ldd; libfluidsynth
#    injected explicitly (dlopen'd → ldd can't see it); libasound excluded
#    (it dlopens host plugins — bundling it breaks host MIDI).
fetch_tool "${LINUXDEPLOY_URL}" "${tools_dir}/linuxdeploy" "${LINUXDEPLOY_SHA256}"
fluidsynth_so="$(ldconfig -p | awk '/libfluidsynth\.so\.3/ {print $NF; exit}')"
if [[ -z "${fluidsynth_so}" ]]; then
    # Release builds set OLDUVAI_REQUIRE_FLUIDSYNTH=1 so a runner missing
    # libfluidsynth3 fails the build instead of silently shipping an
    # AppImage without bundled GM music.
    if [[ "${OLDUVAI_REQUIRE_FLUIDSYNTH:-0}" = "1" ]]; then
        echo "!! libfluidsynth.so.3 not found on host — install libfluidsynth3" >&2
        exit 1
    fi
    echo "!! libfluidsynth.so.3 not found on host — GM music won't be bundled" >&2
fi
"${tools_dir}/linuxdeploy" \
    --appdir "${appdir}" \
    --executable "${appdir}/usr/bin/olduvai" \
    --desktop-file "${build_dir}/olduvai.desktop" \
    --icon-file "${build_dir}/olduvai.png" \
    ${fluidsynth_so:+--library "${fluidsynth_so}"} \
    --exclude-library 'libasound.so*'

# LGPL corresponding-source provision: record exactly which host libraries
# linuxdeploy bundled (file, package, version) so a recipient of the binary
# can fetch matching sources (distro source packages / upstreams named in
# THIRD-PARTY-NOTICES.md).
{
    echo "Shared libraries bundled into this AppImage, taken from the build"
    echo "host: $(. /etc/os-release && echo "${PRETTY_NAME:-unknown}")."
    echo "Corresponding source for each: the distribution's source package"
    echo "for the exact version listed below (e.g. via"
    echo "https://packages.ubuntu.com/), or the upstream project named in"
    echo "THIRD-PARTY-NOTICES.md."
    echo
    for so in "${appdir}"/usr/lib/*.so*; do
        [[ -e "${so}" ]] || continue
        base="$(basename "${so}")"
        pkg="$(dpkg -S "${base}" 2>/dev/null | head -1 | cut -d: -f1 || true)"
        ver=""
        [[ -n "${pkg}" ]] && ver="$(dpkg-query -W -f '${Version}' "${pkg}" \
                                    2>/dev/null || true)"
        echo "${base}${pkg:+  (${pkg} ${ver})}"
    done
} > "${appdir}/usr/bin/licenses/BUNDLED-LIBRARIES.txt"

# 4. Pack.
fetch_tool "${APPIMAGETOOL_URL}" "${tools_dir}/appimagetool" "${APPIMAGETOOL_SHA256}"
out="${here}/olduvai-x86_64.AppImage"
"${tools_dir}/appimagetool" "${appdir}" "${out}"
echo ">> built ${out}"
