#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Build a self-contained olduvai-x86_64.AppImage.
#
# Ships NO game content — the user supplies game files at runtime via
# --game-dir / config / GOG auto-discovery.  Bundles: SDL2 (+ transitive deps,
# ldd-driven), libfluidsynth (dlopen'd → injected explicitly, invisible to
# ldd), and both OFL HD fonts.  libmt32emu is no longer bundled because it is
# compiled in (third_party/mt32emu).  libasound and any SoundFont stay
# host-provided; MT-32 additionally needs the user's own Roland ROMs.
#
# glibc floor: the AppImage runs only on distros whose glibc >= the build
# environment's.  The release job pins that environment to a digest-locked
# ubuntu:22.04 container (floor 2.35), and the gate below ASSERTS the result
# from the artifact instead of trusting the runner label.
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

# License texts (binary distribution obligation — GPLv3 §6 for the
# project's own license, THIRD-PARTY-NOTICES.md for the rest; Nuked-OPL3 and
# libmt32emu LGPL-2.1 compiled in, FluidSynth LGPL-2.1 bundled as a
# replaceable .so).
install -d "${appdir}/usr/bin/licenses"
cp "${here}/LICENSE" "${appdir}/usr/bin/licenses/LICENSE.txt"
cp "${here}/THIRD-PARTY-NOTICES.md" "${appdir}/usr/bin/licenses/"
cp "${here}/third_party/nuked_opl3/LICENSE" "${appdir}/usr/bin/licenses/Nuked-OPL3-LICENSE.txt"
# libmt32emu is vendored and statically linked (LGPL-2.1 §3 conversion to
# GPL, same as Nuked-OPL3) — its text must ship with the binary.
cp "${here}/third_party/mt32emu/COPYING.LESSER.txt" \
   "${appdir}/usr/bin/licenses/libmt32emu-LICENSE.txt"
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
#    injected explicitly (dlopen'd → ldd can't see it);
#    libasound excluded (it dlopens host plugins — bundling it breaks host MIDI).
fetch_tool "${LINUXDEPLOY_URL}" "${tools_dir}/linuxdeploy" "${LINUXDEPLOY_SHA256}"

# FluidSynth from PINNED SOURCE, minimal — not the host's.
#
# Built with -Dosal=embedded and everything optional off, FluidSynth is ONE
# 681 KB .so with no third-party dependencies at all.  The distro package
# instead drags its whole closure into the AppImage.  Measured before/after on
# a real build, 49 bundled libraries -> 39, 7.8 MB -> 5.6 MB, nothing added:
#
#   libglib-2.0  libgobject-2.0  libgmodule-2.0  libinstpatch-1.0  libpcre
#   libreadline  libtinfo  libdb-5.3  libgomp  libpulse-simple
#
# (Only ten: the rest of FluidSynth's usual closure — sndfile, FLAC, vorbis,
# opus, ogg, pulse, systemd, dbus — is SHARED with SDL2's pulseaudio path and
# stays.  Worth stating, because the naive "everything fluidsynth links"
# reading over-counts it more than twofold.)
#
# libtinfo was also the worst offender the glibc floor gate below reported
# (GLIBC_2.42), so this lowers floor pressure too — though libsystemd (2.39)
# remains, since that one arrives via pulse, not FluidSynth.
#
# And macOS and Windows already ship exactly this build
# (packaging/build_fluidsynth.sh / .cmd), so all three platforms now carry the
# same synth at the same version with the same options, instead of Linux
# quietly differing.
sh "${here}/packaging/build_fluidsynth.sh" "${build_dir}/deps"
fluidsynth_so="${build_dir}/deps/lib/libfluidsynth.so.3"
if [[ ! -f "${fluidsynth_so}" ]]; then
    # Not "is it on the host" any more — we build it, so its absence is a
    # broken build, not a missing package.  REQUIRE kept so the release lane
    # states the expectation explicitly.
    if [[ "${OLDUVAI_REQUIRE_FLUIDSYNTH:-0}" = "1" ]]; then
        echo "!! FluidSynth build produced no ${fluidsynth_so}" >&2
        exit 1
    fi
    echo "!! FluidSynth build produced nothing — GM music won't be bundled" >&2
    fluidsynth_so=""
fi
# libmt32emu is NOT handled here any more: it is vendored at
# third_party/mt32emu and compiled into the binary, so there is nothing to
# detect on the host and nothing to inject.  That is the whole point — as a
# dlopen'd library it reached AppImage users ONLY, while macOS and Windows
# packages shipped without it and those users silently fell back to OPL for
# four public releases.  See third_party/mt32emu/OLDUVAI-VENDORING.md.
"${tools_dir}/linuxdeploy" \
    --appdir "${appdir}" \
    --executable "${appdir}/usr/bin/olduvai" \
    --desktop-file "${build_dir}/olduvai.desktop" \
    --icon-file "${build_dir}/olduvai.png" \
    ${fluidsynth_so:+--library "${fluidsynth_so}"} \
    --exclude-library 'libasound.so*'

# ── portability floor: ASSERT it, do not inherit it ────────────────────────
#
# An AppImage runs only where glibc is at least as new as the build host's,
# and glibc/libstdc++/libgcc are on the AppImage excludelist — deliberately
# NOT bundled — so the highest versioned symbol the payload imports IS the
# floor a user's distro must meet.  Until now that floor was a side effect of
# whichever runner image CI happened to use, recorded in a comment and
# checked by nobody: bump `runs-on` and Ubuntu 22.04 / Mint 21 users are
# dropped by a one-word edit that reads like housekeeping.
#
# GitHub has already scheduled that edit for us — the ubuntu-22.04 image
# begins deprecation 2026-09-17 and is removed 2027-04-17 — so the release
# job now pins `container: ubuntu:22.04` by digest instead, and this gate
# proves the result rather than trusting it.
#
# Declared, not measured-and-accepted: raising OLDUVAI_GLIBC_MAX drops
# distros, so it should be a reviewed edit with the reason in the commit.
GLIBC_MAX="${OLDUVAI_GLIBC_MAX:-2.35}"      # Ubuntu 22.04 / Mint 21 class

# Highest X_<n.n.n> version required by one ELF.  .gnu.version_r is exactly
# what ld.so demands at startup, which is why this reads version-info rather
# than the dynamic symbol table.  GLIBC_PRIVATE never matches — the pattern
# only accepts numeric versions.
highest_ver() {   # $1=file  $2=prefix (GLIBC_ / GLIBCXX_ / CXXABI_)
    readelf --wide -V "$1" 2>/dev/null \
        | sed -n "s/.*Name: $2\([0-9][0-9.]*\).*/\1/p" \
        | sort -V | tail -1
}

floor_rc=0
floor_seen=""
for f in "${appdir}/usr/bin/olduvai" "${appdir}"/usr/lib/*.so*; do
    [[ -f "${f}" ]] || continue
    got="$(highest_ver "${f}" GLIBC_)"
    [[ -n "${got}" ]] || continue
    floor_seen="yes"
    if [[ "$(printf '%s\n%s\n' "${got}" "${GLIBC_MAX}" | sort -V | tail -1)" \
          != "${GLIBC_MAX}" ]]; then
        echo "!! $(basename "${f}") needs GLIBC_${got} — above the declared" \
             "GLIBC_${GLIBC_MAX} floor" >&2
        readelf --wide --dyn-syms "${f}" 2>/dev/null \
            | grep -F "GLIBC_${got}" | head -5 >&2      # name the symbols
        floor_rc=1
    fi
done
# A scan that finds nothing is broken, not passing — the exact failure mode
# this repo keeps hitting.
if [[ -z "${floor_seen}" ]]; then
    echo "!! glibc floor scan matched no ELF at all — the gate is broken" >&2
    exit 1
fi
if [[ "${floor_rc}" != 0 ]]; then
    echo "!! this AppImage would not start on the oldest supported distro" >&2
    exit 1
fi
echo ">> glibc floor OK: everything <= GLIBC_${GLIBC_MAX}"

# The SECOND floor.  libstdc++ and libgcc_s are on the same excludelist, so
# GLIBCXX_/CXXABI_ constrain users exactly as GLIBC_ does — a floor nobody had
# ever measured here.  It shipped report-only for one run so CI could tell us
# the true numbers rather than us inventing them; the 2026-07-26 dry run
# printed GLIBCXX_3.4.29 and CXXABI_1.3.9 (jammy's g++-11), so they are now
# declared and enforced like the rest.
#
# Both are properties of the CONTAINER's libstdc++, so they move only if the
# pinned image does — which is the point.  (-static-libstdc++ -static-libgcc
# would remove this floor entirely, but that changes the shipped binary and is
# a separate decision.)
GLIBCXX_MAX="${OLDUVAI_GLIBCXX_MAX:-3.4.29}"   # jammy g++-11
CXXABI_MAX="${OLDUVAI_CXXABI_MAX:-1.3.9}"
for pair in "GLIBCXX_ ${GLIBCXX_MAX}" "CXXABI_ ${CXXABI_MAX}"; do
    set -- ${pair}
    v="$(highest_ver "${appdir}/usr/bin/olduvai" "$1")"
    [[ -n "${v}" ]] || continue
    if [[ "$(printf '%s\n%s\n' "${v}" "$2" | sort -V | tail -1)" != "$2" ]]; then
        echo "!! olduvai needs $1${v} — above the declared $1$2 floor" >&2
        exit 1
    fi
    echo ">> $1 floor OK: $1${v} (<= $2)"
done

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
