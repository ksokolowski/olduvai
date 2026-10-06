#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless win-ending compose regression gate — invoked by CTest.
#
# WHY THIS EXISTS.  The end_sequence.cpp extraction (`dac96bf`, `db183db`)
# carried its own written precondition — "GATE FIRST: game-over/ending have no
# goldens" — and then shipped without one.  So the win ending, the last thing a
# player ever sees, is rendered by code no test executes.  This closes the half
# of that gap the existing hooks can reach: OLDUVAI_ENDING_SHOT dumps the first
# composited ending frame (COOL3 backdrop + caveman) and self-quits.
# A third case covers the Enhanced skip (§3.43 item 4): OLDUVAI_ENDING_SKIP
# presses mid-climb, and the shot then photographs the fade to black.  It
# runs Enhanced with mmpx at render scale 2, an integer path like scale 1.
#
# The GAME-OVER half is still uncovered and deliberately not attempted here.
# It needs a hook of its own: reaching it requires game_over==true, and with
# --level 8 control falls straight through show_game_over_screen into
# show_win_ending, which ends by blocking in SDL_WaitEvent for a keypress that
# never arrives under the dummy driver — the test would hang out its timeout
# rather than fail.  Tracked in docs/internal/BACKLOG.md §6.
#
# Golden = hash, not image: the content policy (CONTRIBUTING.md) forbids game
# imagery in the tree, and the shot contains decoded game artwork.  On failure
# the shot PNG is kept for eyeballing.
#
# Determinism — same construction as mainmenu_shot.sh:
#  - --render-scale 1 forces the INTEGER upscale path (the default x4 goes
#    through the float omniscale upscaler, whose codegen LTO reorders between
#    builds, so a byte-exact golden there is not reproducible).
#  - --window 640x400 pins the output size, which is otherwise derived from
#    desktop dimensions and varies per host and video driver.
#  - XDG_CONFIG_HOME points at an empty temp dir so the user's play.json
#    (enhanced/hd keys) cannot leak into the compose.
# Verified byte-identical across repeated runs before the golden was taken.
#
# Regenerate after an intentional ending change (then update the .sha256):
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_ENDING_SHOT=/tmp/end.png ./build/release/olduvai --play --level 8 \
#       --render-scale 1 --window 640x400 --game-dir <game_dir>
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/ending_golden.sha256"
SHOT="$(mktemp -u /tmp/ending_shot.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "ending_shot: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "ending_shot: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
# --level 8 is past the last playable level: run_game goes straight to the win
# ending.  The hook self-quits after the first frame; timeout is a hang net.
check_one() {   # <golden> <label> <env assignments> <extra args>
    _g="$1"; _label="$2"; _env="$3"; _args="$4"
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    rm -f "${SHOT}"
    # shellcheck disable=SC2086
    env XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_ENDING_SHOT="${SHOT}" ${_env} \
        timeout 60 "${BINARY}" --play --level 8 --window 640x400 \
        --game-dir "${GAME_DIR}" ${_args} >/dev/null 2>&1
    rm -rf "${CFG_DIR}"
    if [ ! -s "${SHOT}" ]; then
        echo "ending_shot: FAIL — no shot produced (${_label})"
        FAIL=1; return
    fi
    if [ "$(sha256 "${SHOT}")" = "$(cat "${_g}")" ]; then
        echo "ending_shot: PASS (${_label})"
        return
    fi
    echo "ending_shot: FAIL — rendered ending differs from the golden hash (${_label})."
    echo "  shot=${SHOT}  golden=${_g}"
    FAIL=1
}
FAIL=0
FIX="$(dirname "$0")/fixtures"
check_one "${GOLDEN}" "first frame" "" "--render-scale 1"
check_one "${FIX}/ending_rise30_golden.sha256" "rise step 30" \
    "OLDUVAI_ENDING_SHOT_FRAME=30" "--render-scale 1"
# The Enhanced skip (BACKLOG §3.43 item 4): a press at rise step 20 fades
# that frame to black with the music; fade frame 27 of 54 is half way.  mmpx: an
# integer scaler, so the hash is stable.  --transitions classic keeps
# Enhanced but turns smooth motion off: with it on, the climb's sub-frames
# are vsync-paced wherever the driver grants vsync (sdl-floor's dummy driver
# did), so the frame the skip leaves on screen follows the wall clock.
check_one "${FIX}/ending_skip_fade_half_golden.sha256" "Enhanced skip, fade frame 27" \
    "OLDUVAI_ENDING_SKIP=20 OLDUVAI_ENDING_SHOT_FRAME=27" \
    "--enhanced --transitions classic --hd-profile mmpx --render-scale 2"
# The HD ending repaints only where the caveman moved (EndingCanvas over
# DirtyFrame).  Mid-climb, that frame must equal the one composed whole
# (OLDUVAI_DIRTY=0): a rect the dirty path missed shows here.
shot_hash() {   # <env assignments>
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    rm -f "${SHOT}"
    # shellcheck disable=SC2086
    env XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_ENDING_SHOT="${SHOT}" $1 \
        timeout 60 "${BINARY}" --play --level 8 --window 640x400 \
        --game-dir "${GAME_DIR}" --enhanced --transitions classic \
        --hd-profile mmpx --render-scale 2 >/dev/null 2>&1
    rm -rf "${CFG_DIR}"
    [ -s "${SHOT}" ] && sha256 "${SHOT}"
}
DIRTY_HASH="$(shot_hash "OLDUVAI_ENDING_SHOT_FRAME=30")"
WHOLE_HASH="$(shot_hash "OLDUVAI_DIRTY=0 OLDUVAI_ENDING_SHOT_FRAME=30")"
if [ -z "${DIRTY_HASH}" ] || [ "${DIRTY_HASH}" != "${WHOLE_HASH}" ]; then
    echo "ending_shot: FAIL — the HD rise's dirty frame differs from the whole one"
    FAIL=1
else
    echo "ending_shot: PASS (HD rise, dirty = whole)"
fi
[ ${FAIL} -eq 0 ] && rm -f "${SHOT}"
[ ${FAIL} -eq 0 ] || exit 1
exit 0

echo "ending_shot: FAIL — rendered ending differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot; if the ending renders correctly and the change is"
echo "  intentional, regenerate the hash (see header)."
exit 1
