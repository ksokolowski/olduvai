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
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_ENDING_SHOT="${SHOT}" timeout 60 \
    "${BINARY}" --play --level 8 --render-scale 1 --window 640x400 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "ending_shot: FAIL — no shot produced (ending not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "ending_shot: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "ending_shot: FAIL — rendered ending differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot; if the ending renders correctly and the change is"
echo "  intentional, regenerate the hash (see header)."
exit 1
