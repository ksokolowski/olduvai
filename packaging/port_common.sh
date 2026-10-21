# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# What every handheld port bundle shares, sourced by the generators
# (build_port_knulli.sh, build_port_portmaster.sh): the repo guard, the class
# A files every bundle carries (fonts and licence texts, the empty data
# folders with their notes) and the README sections that do not depend on the
# firmware.  Bash; the caller sets REPO_ROOT and `set -euo pipefail`.

# Refuse an output directory inside the repo: a private bundle contains game
# files, and CONTRIBUTING.md's allowlist is absolute.  Prints the absolute path.
port_out_dir() {   # <tool name> <out>
    local tool="$1" out="$2" abs
    mkdir -p "$out"
    abs=$(cd "$out" && pwd -P)
    case "${abs}/" in
        "${REPO_ROOT}/"*)
            {
                echo "${tool}: refusing to write a bundle inside the repo."
                echo "  A private bundle contains game files; the repo must never hold them."
                echo "  repo: ${REPO_ROOT}"
                echo "  out:  ${abs}"
            } >&2
            return 1 ;;
    esac
    echo "$abs"
}

# fonts/: the Enhanced HD text loads them from <exe dir>/fonts/.  Their
# licences go to licenses/ with the rest (port_copy_licenses).
port_copy_fonts() {   # <port dir>
    mkdir -p "$1/fonts"
    cp -L "${REPO_ROOT}"/assets/fonts/*.ttf "$1/fonts/"
}

# licenses/: one flat folder, LICENSE.<component>.txt per component (the
# PortMaster convention, used by both handheld bundles).  SDL2 is the device's
# own, loaded dynamically, so no SDL text is owed; THIRD-PARTY-NOTICES.md lists
# it anyway.  xBRZ is GPL-3.0 only: its text is the GPL's.
port_copy_licenses() {   # <port dir>
    local l="$1/licenses" f name
    mkdir -p "$l"
    cp "${REPO_ROOT}/LICENSE" "$l/LICENSE.olduvai.txt"
    cp "${REPO_ROOT}/LICENSE" "$l/LICENSE.xBRZ.txt"
    cp "${REPO_ROOT}/THIRD-PARTY-NOTICES.md" "$l/"
    cp "${REPO_ROOT}/third_party/nuked_opl3/LICENSE" "$l/LICENSE.Nuked-OPL3.txt"
    cp "${REPO_ROOT}/third_party/mt32emu/COPYING.LESSER.txt" "$l/LICENSE.libmt32emu.txt"
    cp "${REPO_ROOT}/third_party/rtmidi/LICENSE" "$l/LICENSE.RtMidi.txt"
    cp "${REPO_ROOT}/third_party/fluidsynth/LICENSE" "$l/LICENSE.FluidSynth.txt"
    cp "${REPO_ROOT}/third_party/fluidsynth/gcem/LICENSE" "$l/LICENSE.gcem.txt"
    cp "${REPO_ROOT}/third_party/fluidsynth/gcem/NOTICE.txt" "$l/NOTICE.gcem.txt"
    for f in "${REPO_ROOT}"/assets/fonts/*-LICENSE.txt; do   # OFL texts
        name=$(basename "$f" -LICENSE.txt)
        cp -L "$f" "$l/LICENSE.${name}.txt"
    done
}

# Empty directories do not survive every archiver; a note keeps each one.
port_put_files_notes() {   # <port dir> <readme name> <dir>...
    local port="$1" readme="$2" d
    shift 2
    for d in "$@"; do
        mkdir -p "${port}/${d}"
        [ -n "$(ls -A "${port}/${d}")" ] || cat > "${port}/${d}/PUT-FILES-HERE.txt" <<NOTE
See ../${readme}, section "${d}".
NOTE
    done
}

# The README section on game files: what the player copies, and why a GOG
# copy without HISTORIK.EXE is complete.
port_readme_game_files() {   # <where, as the player sees it>
    cat <<SECTION
Put your own copies of these into  $1 :

    FILESA.CUR
    FILESB.CUR
    FILESA.VGA
    FILESB.VGA
    and EITHER  HISTORIK.EXE  OR  PREH.SQZ

GOG and other CD-era releases have no HISTORIK.EXE — they ship the executable in
its compressed form, PREH.SQZ, and Olduvai reads that directly.  If that is what
your copy contains, it is correct; nothing is missing.
SECTION
}

# The README section on music, for a bundle that ships no ROMs or SoundFont.
port_readme_music() {   # <port folder name as the player sees it>
    cat <<SECTION
By default you get the Sound Blaster sound of 1991 - FM music (the AdLib
chip) with digital sound effects - which needs nothing.
If you own Roland MT-32 or CM-32L ROM images, copy them into  $1/mt32-roms/
(CM32L_CONTROL.ROM + CM32L_PCM.ROM, and/or MT32_CONTROL.ROM + MT32_PCM.ROM) and
the Roland sound is used instead.  For General MIDI, copy a SoundFont (.sf2)
into  $1/soundfonts/ : it is used when no Roland ROMs are present.
SECTION
}
