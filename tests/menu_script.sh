#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# In-game menu behaviour baseline — invoked by CTest.
#
# Drives the pause menu headlessly with OLDUVAI_MENU_SCRIPT (synthetic SDL key
# events, one token/frame) through representative walks and compares each
# captured frame's SHA-256 to the committed golden hash list. The regression
# net for the interactive menu — nav, submenu entry, value change, the confirm
# dialog and Apply — so any future menu change (incl. the CC2c pause-controller
# extraction) is testable instead of needing a manual playtest.
#
# Goldens = hash lists (sha256sum -c format), not images: the content policy
# (CONTRIBUTING.md) forbids game imagery in the tree. On failure the shot dir
# is kept in /tmp for eyeballing.
#
# Scenarios:
#   menu_baseline  — pause nav + Options + Cheats submenus + back-out (6 shots)
#   menu_settings  — Options→Audio, drop Music volume 100→95, back out to the
#                    confirm dialog, Apply (3 shots)
#
# NB: scenarios deliberately avoid reinit/warp/load/restart — those re-enter
# run_platform_level, which re-reads OLDUVAI_MENU_SCRIPT from the top (the script
# has no cross-re-entry state). The reinit path is covered by reinit_smoke.
#
# Determinism (host-independent by construction):
#  - --render-scale 1 → INTEGER upscale path (see mainmenu_shot.sh).
#  - --window 640x400 pins the output size (otherwise desktop_integer_scale()
#    sizes the window from the host desktop — varies per host/driver).
#  - XDG_CONFIG_HOME → fresh temp dir per scenario. This BOTH stops the user's
#    play.json (enhanced/hd keys) leaking into the compose AND stops the
#    menu_settings Apply step writing music_volume into the user's REAL config
#    (it did, before this isolation).
#
# Regenerate after an intentional menu change:
#   run a scenario with OLDUVAI_MENU_SCRIPT_DIR=<dir>, eyeball the frames, then
#   (cd <dir> && sha256sum *.png) > tests/fixtures/<scenario>.sha256
#
# Skip (77) when game data or the binary is absent.

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(dirname "$0")/fixtures"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "menu_script: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "menu_script: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

# sha256 <file> — portable (Linux sha256sum / macOS shasum).
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"   # mute test runs
FAIL=0

# run_scenario <golden-name> <script>
run_scenario() {
    GOLDEN="${FIX}/$1.sha256"
    SCRIPT="$2"
    OUT_DIR="$(mktemp -d /tmp/menu_script.XXXXXX)"
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_MENU_SCRIPT="${SCRIPT}" \
        OLDUVAI_MENU_SCRIPT_DIR="${OUT_DIR}" timeout 60 \
        "${BINARY}" --play --level 1 --render-scale 1 --window 640x400 \
        --game-dir "${GAME_DIR}" >/dev/null 2>&1
    rm -rf "${CFG_DIR}"
    SCEN_FAIL=0
    while read -r WANT NAME; do
        [ -n "${NAME}" ] || continue
        if [ ! -s "${OUT_DIR}/${NAME}" ]; then
            echo "menu_script[$1]: FAIL — shot ${NAME} not produced"
            SCEN_FAIL=1
        elif [ "$(sha256 "${OUT_DIR}/${NAME}")" != "${WANT}" ]; then
            echo "menu_script[$1]: FAIL — ${NAME} differs from golden hash"
            echo "  shot=${OUT_DIR}/${NAME}  golden=${GOLDEN}"
            SCEN_FAIL=1
        fi
    done < "${GOLDEN}"
    if [ ${SCEN_FAIL} -eq 0 ]; then
        rm -rf "${OUT_DIR}"
    else
        FAIL=1   # keep OUT_DIR for eyeballing
    fi
}

run_scenario menu_baseline \
    "esc shot down shot down shot down enter shot esc shot down enter shot quit"
# (the Options screen's first row is now the Presentation preset — one extra
#  `down` keeps this walk's intent: Audio → Music volume 100→95)
run_scenario menu_settings \
    "esc down down down enter down enter down down left shot esc esc shot enter shot quit"

[ ${FAIL} -eq 0 ] && echo "menu_script: PASS"
exit ${FAIL}
