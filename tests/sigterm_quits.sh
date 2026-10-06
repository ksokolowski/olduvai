#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# SIGTERM leaves the program from a level — invoked by CTest.
#
# SDL turns SIGTERM into SDL_QUIT: PortMaster's quit hotkey (gptokeyb sends it
# to the game's process) and a desktop's window close both arrive that way.  A
# level used to treat it as "abort to the title" (the THE END screen), so a
# handheld needed the hotkey twice and a stuck headless run never ended.  Now
# the program ends at once: the process must be gone a few seconds after the
# signal.
#
# Skips (77) when game data or the binary is absent.
set -eu

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "sigterm_quits: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "sigterm_quits: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
PID=""
cleanup() {
    [ -n "${PID}" ] && kill -KILL "${PID}" 2>/dev/null || true
    rm -rf "${CFG}"
}
trap cleanup EXIT

XDG_CONFIG_HOME="${CFG}" "${BINARY}" --play --level 1 --music-device off \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1 &
PID=$!

# Let it reach the level's frame loop (the loading card and GET READY come first).
sleep 5
if ! kill -0 "${PID}" 2>/dev/null; then
    echo "sigterm_quits: FAIL — the game ended on its own before the signal"
    exit 1
fi

kill -TERM "${PID}"
i=0
while [ ${i} -lt 40 ]; do                    # up to 8 s
    if ! kill -0 "${PID}" 2>/dev/null; then
        echo "sigterm_quits: OK — gone ${i} polls after SIGTERM"
        PID=""
        exit 0
    fi
    sleep 0.2
    i=$((i + 1))
done
echo "sigterm_quits: FAIL — still running 8 s after SIGTERM (aborted to the title instead of quitting)"
exit 1
