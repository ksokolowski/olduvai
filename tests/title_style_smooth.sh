#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# A Style apply at the title must reach the game it starts: switching Classic
# -> Enhanced HD, then Start Game, runs the level with smooth motion (which
# derives from `enhanced`).  The title's apply once wrote `enhanced` without
# re-deriving it, so the level ran with smooth motion off.
#
# The walk: down down enter (Options) right (Style -> Enhanced) esc enter
# (Apply) up up enter (Start Game), then waits; the in-level replay of the
# script ends the run.  OLDUVAI_PACE_TRACE prints one line per smooth
# sub-frame, so a level without smooth motion prints none.
#
# Usage: title_style_smooth.sh <game_dir> <binary>.  Skip (77) without game
# data or the binary.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "title_style_smooth: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "title_style_smooth: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
. "$(dirname "$0")/lib/engine_err.sh"
engine_err_init

CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
WAITS="wait wait wait wait wait wait wait wait wait wait"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_PACE_TRACE=1 \
    OLDUVAI_MENU_SCRIPT="down down enter right esc enter wait up up enter ${WAITS} ${WAITS}" \
    timeout 60 "${BINARY}" --play --game-dir "${GAME_DIR}" >"${ERR}" 2>&1
rm -rf "${CFG_DIR}"

if ! grep -q "game: level 1" "${ERR}"; then
    echo "title_style_smooth: FAIL — the walk did not start the game"
    engine_said "${ERR}"
    exit 1
fi
N=$(grep -c '\[PACE\] sub' "${ERR}")
if [ "${N}" -eq 0 ]; then
    echo "title_style_smooth: FAIL — the level ran without smooth motion"
    exit 1
fi
echo "title_style_smooth: OK — ${N} smooth sub-frames after the title apply"
exit 0
