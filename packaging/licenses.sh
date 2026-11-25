# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The third-party licence texts every release package carries, as one list:
# every packager copies them from here (sourced; POSIX sh, as two of them
# are).  A component compiled into the binary gets a line here and a row in
# THIRD-PARTY-NOTICES.md; tests/licenses.sh holds the two together.
#
# Not listed, by design: Olduvai's own LICENSE (each package places it
# itself), SDL2 (zlib; the handhelds load the device's own), stb (public
# domain, the alternative its headers offer) and the fonts' OFL texts (copied
# with the fonts).

# <kind> <source, from the repo root> <component>
license_texts() {
    cat <<'EOF'
LICENSE third_party/nuked_opl3/LICENSE Nuked-OPL3
LICENSE third_party/mt32emu/COPYING.LESSER.txt libmt32emu
LICENSE third_party/rtmidi/LICENSE RtMidi
LICENSE third_party/fluidsynth/LICENSE FluidSynth
LICENSE third_party/fluidsynth/gcem/LICENSE gcem
NOTICE third_party/fluidsynth/gcem/NOTICE.txt gcem
LICENSE src/enhance/LICENSE.OmniScale.txt OmniScale
LICENSE src/enhance/LICENSE.MMPX.txt MMPX
EOF
}

# Copy them into <dir>, named per <style>: "port" LICENSE.<component>.txt (the
# PortMaster convention, both handheld bundles), "desktop"
# <component>-LICENSE.txt.  Non-zero when a text is missing.
copy_license_texts() {   # <repo root> <dir> <port|desktop>
    lt_root=$1 lt_dir=$2 lt_style=$3 lt_rc=0
    mkdir -p "$lt_dir" || return 1
    while read -r lt_kind lt_src lt_name; do
        case $lt_style in
            port) lt_out="$lt_dir/$lt_kind.$lt_name.txt" ;;
            desktop) lt_out="$lt_dir/$lt_name-$lt_kind.txt" ;;
            *) echo "copy_license_texts: style '$lt_style'" >&2; return 2 ;;
        esac
        cp -L "$lt_root/$lt_src" "$lt_out" || lt_rc=1
    done <<EOF
$(license_texts)
EOF
    return $lt_rc
}
