#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Nothing is presented after the level-complete tally — invoked by CTest.
#
# WHY THIS EXISTS. The tally runs INSIDE the level loop's level-complete
# intercept, which then only cleared `running`: the rest of that iteration
# still composed and presented the level, so between the tally and the next
# level's "Please Wait" the player saw one frame of the finished level —
# drawn from the post-tally state (score and lives already counted up).
# Owner report, 2026-09-16, both Classic and Enhanced, every platform level.
#
# How: OLDUVAI_DUMP_TALLY ends the tally once it has its frames (the "tally
# was quit" return), OLDUVAI_DUMP_OUTPUT records every present.  The LAST
# present must then be the tally's black card, not a gameplay frame.  A
# gameplay frame of L1 is mostly sky; the tally is mostly black.  No image
# golden — the frames carry game artwork (CONTRIBUTING.md).
#
# Skip (exit 77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "tally_no_flash: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "tally_no_flash: SKIP — binary not found: ${BINARY}"
    exit 77
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

rc=0
for mode in classic hd; do
    if [ "${mode}" = hd ]; then
        FLAGS="--profile hd --aspect widescreen --window 896x400"
    else
        FLAGS="--profile dos --render-scale 1 --window 640x400"
    fi
    WORK="$(mktemp -d /tmp/olduvai_tallyflash.XXXXXX)"
    mkdir "${WORK}/out" "${WORK}/tally" "${WORK}/cfg"
    # shellcheck disable=SC2086
    XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_FORCE_LEVEL_COMPLETE=8 \
        OLDUVAI_DUMP_TALLY="${WORK}/tally" OLDUVAI_DUMP_OUTPUT="${WORK}/out" \
        "${BINARY}" --play --level 1 --game-dir "${GAME_DIR}" ${FLAGS} \
        >"${WORK}/run.log" 2>&1

    last=$(find "${WORK}/out" -name 'out_*.bmp' | sort | tail -1)
    ntally=$(find "${WORK}/tally" -name 'tally_*' | wc -l | tr -d ' ')
    if [ -z "${last}" ] || [ "${ntally}" -eq 0 ]; then
        echo "tally_no_flash: FAIL (${mode}) — the tally was never reached"
        echo "  (out: ${last:-none}, tally frames: ${ntally}); log ${WORK}/run.log"
        rc=1
        continue
    fi
    # Share of lit pixels in the last present (BMP: 54-byte header, BGR(A)).
    lit=$(python3 - "${last}" <<'EOF'
import struct, sys
d = open(sys.argv[1], 'rb').read()
off = struct.unpack('<I', d[10:14])[0]
bpp = struct.unpack('<H', d[28:30])[0] // 8
px = d[off:]
n = len(px) // bpp
lit = sum(1 for i in range(0, n * bpp, bpp) if px[i] | px[i + 1] | px[i + 2])
print(lit * 100 // n)
EOF
)
    if [ "${lit}" -gt 30 ]; then
        echo "tally_no_flash: FAIL (${mode}) — the last present is ${lit}% lit:"
        echo "  a gameplay frame was shown after the tally.  ${last}"
        rc=1
    else
        echo "tally_no_flash: PASS (${mode}, last present ${lit}% lit)"
        rm -rf "${WORK}"
    fi
done
exit ${rc}
