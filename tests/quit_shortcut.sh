#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The Quit key (F10 by default, key_quit in play.json) — invoked by CTest.
#
# It opens the pause overlay and asks "Exit game?", on No, so no single key
# press ends a run; only Yes does.  Driven by OLDUVAI_MENU_SCRIPT through the
# real binary: a `shot` token placed after the answer exists only if the game
# was still running when it was reached.  Covers a platform level and a boss
# fight, and a rebound key (the default stops working, the new one asks).
#
# Skips (77) when game data or the binary is absent.
set -eu

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "quit_shortcut: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "quit_shortcut: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
FAIL=0

# shots <level> <tokens> [play.json] — how many `shot` files the run produced.
shots() {
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    OUT="$(mktemp -d /tmp/olduvai_qs.XXXXXX)"
    if [ -n "${3:-}" ]; then
        mkdir -p "${CFG}/olduvai"
        printf '%s' "$3" > "${CFG}/olduvai/play.json"
    fi
    XDG_CONFIG_HOME="${CFG}" OLDUVAI_MENU_SCRIPT="$2" OLDUVAI_MENU_SCRIPT_DIR="${OUT}" \
        timeout -s KILL 90 "${BINARY}" --play --level "$1" --render-scale 1 \
        --window 640x400 --music-device off --game-dir "${GAME_DIR}" \
        >/dev/null 2>"${OUT}/run.err" || true
    ls "${OUT}" | grep -c '\.png$' || true
    rm -rf "${CFG}" "${OUT}"
}

# want <label> <expected-shots> <level> <tokens> [play.json]
want() {
    GOT="$(shots "$3" "$4" "${5:-}")"
    if [ "${GOT}" = "$2" ]; then
        echo "quit_shortcut: OK   — $1"
    else
        echo "quit_shortcut: FAIL — $1: expected $2 shot(s), got ${GOT}"
        FAIL=1
    fi
}

WAIT="wait wait wait wait wait wait wait wait wait wait"   # a boss fight needs a moment to start

want "level: F10 then Accept (default No) keeps playing"      1 1 "f10 enter shot quit"
want "level: F10, Yes, Accept quits"                          0 1 "f10 right enter shot quit"
want "level: F10 rebound to Tab is inert"                     1 1 "f10 right enter shot quit" '{"key_quit":"Tab"}'
want "level: the rebound Tab asks, and Yes quits"             0 1 "tab right enter shot quit" '{"key_quit":"Tab"}'
want "boss: F10 then Accept (default No) keeps fighting"      1 2 "${WAIT} f10 enter shot quit"
want "boss: F10, Yes, Accept quits"                           0 2 "${WAIT} f10 right enter shot quit"

exit ${FAIL}
