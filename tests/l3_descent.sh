#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L3 trunk-descent gate — invoked by CTest.
#
# Plays the screen 17 -> 18 descent cinematic on both present paths and
# compares the concatenated pre-upscale frames to committed SHA-256 hashes.
# The cinematic is reachable only at the end of a full L3 playthrough, so no
# other test ran a line of l3_end_level.cpp (coverage_layers.sh: 0%).
#
# Golden = hash, not image (the frames hold decoded game artwork).  PNG, not
# BMP: SDL_SaveBMP headers differ between SDL builds, stb's PNG does not.
# Determinism as level_fade.sh: OLDUVAI_FORCE_L3_DESCENT seeds the food/kill
# gate, the margin and window are pinned, the config is an empty temp dir,
# and the frames are pre-upscale (the HD profile is pinned to mmpx anyway, as
# in every pixel gate: omniscale's float codegen differs per platform).
# `wide` runs the enhanced composite and the camera pan; `classic` the 320
# path without the pan.
#
# Regenerate after an intentional change, then update the .sha256 files:
#   OLDUVAI_FORCE_L3_DESCENT=1 OLDUVAI_WS_FORCE_MARGIN=32 \
#   OLDUVAI_DUMP_DESCENT=<dir> SDL_VIDEODRIVER=dummy ./build/release/olduvai \
#       --play --level 5 --start-screen 17 --game-dir <game_dir> \
#       --play-frames 30 {--render-scale 1 --window 640x400 |
#       --enhanced --hd-profile mmpx --aspect widescreen --window 896x400}
#   cat <dir>/descent_*.png | shasum -a 256
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIXDIR="$(dirname "$0")/fixtures"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "l3_descent: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "l3_descent: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum
    else shasum -a 256; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

rc=0
for mode in classic wide; do
    if [ "${mode}" = wide ]; then
        FLAGS="--enhanced --hd-profile mmpx --aspect widescreen --window 896x400"
    else
        FLAGS="--render-scale 1 --window 640x400"
    fi
    DUMP="$(mktemp -d /tmp/olduvai_descent.XXXXXX)"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    # shellcheck disable=SC2086
    XDG_CONFIG_HOME="${CFG}" OLDUVAI_FORCE_L3_DESCENT=1 \
        OLDUVAI_WS_FORCE_MARGIN=32 OLDUVAI_DUMP_DESCENT="${DUMP}" \
        timeout -s KILL 300 "${BINARY}" --play --level 5 --start-screen 17 \
        --game-dir "${GAME_DIR}" --play-frames 30 ${FLAGS} >/dev/null 2>&1
    rm -rf "${CFG}"

    n=$(find "${DUMP}" -name 'descent_*.png' | wc -l | tr -d ' ')
    if [ "${n}" -eq 0 ]; then
        echo "l3_descent: FAIL (${mode}) — no frames dumped; the descent never"
        echo "  ran.  OLDUVAI_FORCE_L3_DESCENT wiring?"
        rm -rf "${DUMP}"
        rc=1
        continue
    fi

    GOT=$(cat "${DUMP}"/descent_*.png | sha256)
    GOLDEN_FILE="${FIXDIR}/l3_descent_${mode}.sha256"
    if [ ! -f "${GOLDEN_FILE}" ]; then
        echo "l3_descent: FAIL (${mode}) — no golden at ${GOLDEN_FILE}"
        echo "  got ${n} frames, hash ${GOT}"
        rm -rf "${DUMP}"
        rc=1
        continue
    fi
    if [ "${GOT}" = "$(cat "${GOLDEN_FILE}")" ]; then
        echo "l3_descent: PASS (${mode}, ${n} frames)"
        rm -rf "${DUMP}"
    else
        echo "l3_descent: FAIL (${mode}) — ${n} frames differ from the golden."
        echo "  frames=${DUMP}  golden=${GOLDEN_FILE}"
        echo "  got ${GOT}"
        rc=1
    fi
done
exit ${rc}
