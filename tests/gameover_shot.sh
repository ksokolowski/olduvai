#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Game-over screen golden — the half of end_sequence.cpp that had no gate.
#
# WHY THIS EXISTS.  ending_shot covers the WIN ending; the game-over path
# needed game_over == true, and every frame-capped run sets `single`, which
# SKIPS the screen — so the obvious headless shapes could not reach it.  The
# route here is an UNCAPPED replay of tests/fixtures/l6_slam_deaths.jsonl (the
# §3.3c slam scenario): the giant kills all three lives in ~370 ticks, the
# replay-break exits after the screen, and OLDUVAI_GAMEOVER_SHOT captures one
# presented frame instead of waiting out the 8-second hold under the dummy
# driver.
#
# WHAT IT PINS.  THEEND.PC1 decode + compose + upload through the LIVE
# presenter at classic logical size (960x600 default window), with MORT.MDI
# selected as death music before the capture skips its playback.  ~20 s wall.
#
# Determinism: byte-identical over two consecutive runs.
#
# Regenerate after an intentional change:
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_GAMEOVER_SHOT=/tmp/gameover.png ./build/release/olduvai \
#       --play --level 6 --replay tests/fixtures/l6_slam_deaths.jsonl \
#       --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/gameover_golden.sha256"
SHOT="$(mktemp -u /tmp/gameover_shot.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "gameover_shot: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "gameover_shot: SKIP — binary not found"
    exit ${SKIP}
fi
FIX="$(dirname "$0")/fixtures/l6_slam_deaths.jsonl"

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_GAMEOVER_SHOT="${SHOT}" timeout 120 \
    "${BINARY}" --game-dir "${GAME_DIR}" --play --level 6 \
    --replay "${FIX}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "gameover_shot: FAIL — no shot produced (game over not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "gameover_shot: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "gameover_shot: FAIL — the game-over picture differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
exit 1
