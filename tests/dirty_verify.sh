#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Gate for the dirty-rect present (render/dirty_rects.hpp): the steady
# Enhanced widescreen present copies the background back only under the last
# present's rects and uploads only what changed, and with no margins (4:3, a
# 16:10 or narrower window) the steady frame uploads only the rects its compose
# changed.  A rect either failed to record is a stale pixel on screen that no
# CPU-buffer golden can see.
#
# OLDUVAI_DIRTY_VERIFY=1 tracks each texture's contents on the CPU from every
# upload and compares them, every present, with the same frame composed whole.
# Each case below must end with mismatches=0, and with partial > 0 so the
# dirty path actually ran.  The cases cover every writer the path records:
# sprites and the HUD bars (L1), a secret room's full-path frames and the way
# back (L1 secret), lava bubbles mirrored into the margins (L7), the L3 descent,
# and the boss arena (L2 replay, L4).  The no-margin cases add a cave there and
# back (L1, L3).
#
# Checked to fail: recording each sprite rect one row short gives mismatches
# from the third present of the L1 case; drawing unclipped in the repaint
# pass (unchanged soft edges blended twice) gives 24 in 100 L1 presents.
# The latter needs a blending scaler, hence --profile hd (omniscale): the
# boss sprites and the handheld `smooth` profile have no partial alpha.
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(dirname "$0")/fixtures"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "dirty_verify: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "dirty_verify: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
. "$(dirname "$0")/lib/engine_err.sh"
engine_err_init

rc=0
# The widescreen cases force a margin; the flat ones have none.
MODE_ENV="OLDUVAI_WS_FORCE_MARGIN=48"
MODE_ARGS="--aspect widescreen --window 896x400"
# case <label> <env assignments or -> <olduvai args...>
run_case() {
    label="$1"; envs="$2"; shift 2
    [ "${envs}" = "-" ] && envs=""
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    # shellcheck disable=SC2086
    env XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_DIRTY_VERIFY=1 \
        ${MODE_ENV} ${envs} \
        timeout 300 "${BINARY}" --no-config --game-dir "${GAME_DIR}" \
        --profile hd ${MODE_ARGS} \
        --music-device off --transitions classic --play "$@" \
        </dev/null >"${ERR}" 2>&1
    rm -rf "${CFG_DIR}"
    line="$(grep '^dirty-verify: checked=' "${ERR}" | tail -1)"
    checked="$(echo "${line}" | sed -n 's/.*checked=\([0-9]*\).*/\1/p')"
    partial="$(echo "${line}" | sed -n 's/.*partial=\([0-9]*\).*/\1/p')"
    bad="$(echo "${line}" | sed -n 's/.*mismatches=\([0-9]*\).*/\1/p')"
    if [ -z "${line}" ]; then
        echo "dirty_verify: FAIL (${label}) — no dirty-verify line"
        engine_said "${ERR}"
        rc=1
    elif [ "${bad}" != "0" ]; then
        echo "dirty_verify: FAIL (${label}) — ${line}"
        grep '^dirty-verify: mismatch' "${ERR}" | head -5
        rc=1
    elif [ "${partial:-0}" -eq 0 ] || [ "${checked:-0}" -eq 0 ]; then
        echo "dirty_verify: FAIL (${label}) — the dirty path did not run: ${line}"
        rc=1
    else
        echo "dirty_verify: OK (${label}) — ${line#dirty-verify: }"
    fi
}

# The runs are paced at the game's 18.2 Hz: about 100 s of wall clock, little
# CPU.
run_case l1_full_clear - --level 1 \
    --replay "${FIX}/l1_full_clear.jsonl" --play-frames 150
run_case l1_secret - --level 1 --start-screen 5 \
    --replay "${FIX}/secret_l1_in.jsonl" --play-frames 300
run_case l7_volcanic_walk - --level 7 \
    --replay "${FIX}/l7_volcanic_walk.jsonl" --play-frames 191
run_case l3_descent OLDUVAI_FORCE_L3_DESCENT=1 --level 5 \
    --start-screen 17 --play-frames 100
run_case boss_l2 - --level 2 \
    --replay "${FIX}/l2_boss_fight.jsonl" --play-frames 150
run_case boss_l4 - --level 4 --play-frames 100

MODE_ENV=""
MODE_ARGS="--aspect 4:3 --window 640x480"
run_case flat_l1_full_clear - --level 1 \
    --replay "${FIX}/l1_full_clear.jsonl" --play-frames 150
run_case flat_l1_secret - --level 1 --start-screen 5 \
    --replay "${FIX}/secret_l1_in.jsonl" --play-frames 300
run_case flat_l1_cave - --level 1 --start-screen 2 \
    --replay "${FIX}/cave_l1_in.jsonl" --play-frames 300
run_case flat_l3_cave - --level 3 \
    --replay "${FIX}/l3s4_cave_roundtrip.jsonl" --play-frames 400
run_case flat_l7_volcanic_walk - --level 7 \
    --replay "${FIX}/l7_volcanic_walk.jsonl" --play-frames 191
exit ${rc}
