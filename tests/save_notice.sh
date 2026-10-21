#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# A quicksave says so on screen — invoked by CTest.
#
# Each display path runs the same key walk twice, once pressing F6 (the
# quicksave), and shoots the frame just after it and again once the notice
# has gone.  The first pair must differ (the notice is drawn) and the second
# must be identical (it is gone, nothing of it left in the frame).  A save to
# an unwritable save must show its own notice too.  Paths: Classic, the
# Enhanced 4:3 frame (the steady dirty-rect present), and widescreen (its own
# overlay, keyed on the HUD text).
#
# Skips (77) when game data or the binary is absent.
set -u
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77
if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "save_notice: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "save_notice: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK=$(mktemp -d /tmp/save_notice.XXXXXX)
trap '[ "${fail:-1}" -eq 0 ] && rm -rf "${WORK}"' EXIT

# Past GET READY (its banner runs on the wall clock), then the key, a shot,
# 2 s of ticks for the notice to go, a second shot.
LEAD=$(printf 'wait %.0s' $(seq 40))
GONE=$(printf 'wait %.0s' $(seq 45))

run() {   # <dir> <key token> <cfg dir> <flags...>
    out="$1" key="$2" cfg="$3"; shift 3
    mkdir -p "${out}"
    XDG_CONFIG_HOME="${cfg}" OLDUVAI_MENU_SCRIPT="${LEAD} ${key} wait shot ${GONE} shot quit" \
        OLDUVAI_MENU_SCRIPT_DIR="${out}" timeout 120 "${BINARY}" --no-config \
        --play --level 1 --music-device off --game-dir "${GAME_DIR}" "$@" \
        > "${out}/run.log" 2>&1
}

fail=0
check() {   # <name> <flags...>
    name="$1"; shift
    run "${WORK}/${name}.save" key:F6 "${WORK}/${name}.cfg" "$@"
    run "${WORK}/${name}.none" wait "${WORK}/${name}.cfg2" "$@"
    for d in save none; do
        [ -s "${WORK}/${name}.${d}/000.png" ] && [ -s "${WORK}/${name}.${d}/001.png" ] || {
            echo "FAIL: ${name}: no shots ($(tail -1 "${WORK}/${name}.${d}/run.log"))"
            fail=1; return; }
    done
    grep -q "save: game written" "${WORK}/${name}.save/run.log" || {
        echo "FAIL: ${name}: F6 did not save"; fail=1; }
    if cmp -s "${WORK}/${name}.save/000.png" "${WORK}/${name}.none/000.png"; then
        echo "FAIL: ${name}: no notice after the save"; fail=1
    fi
    if cmp -s "${WORK}/${name}.save/001.png" "${WORK}/${name}.none/001.png"; then
        echo "  ${name}: notice shown, then gone"
    else
        echo "FAIL: ${name}: the frame still differs after the notice"; fail=1
    fi
}

check classic --render-scale 1 --window 640x400
check enhanced --enhanced --hd-profile xbrz --render-scale 2 --aspect 4:3 \
    --window 640x480
check widescreen --enhanced --hd-profile xbrz --render-scale 2 \
    --aspect widescreen --window 712x400

# A failed save says so: a file where the saves folder goes, so no
# quicksave can be written (as root and on Windows too, where a read-only
# folder does not stop it).
ro="${WORK}/ro"
mkdir -p "${ro}/olduvai" && : > "${ro}/olduvai/saves"
run "${WORK}/failed" key:F6 "${ro}" --render-scale 1 --window 640x400
run "${WORK}/failed.none" wait "${WORK}/failed.cfg" --render-scale 1 --window 640x400
if grep -q "save: FAILED" "${WORK}/failed/run.log" &&
   ! cmp -s "${WORK}/failed/000.png" "${WORK}/failed.none/000.png" &&
   ! cmp -s "${WORK}/failed/000.png" "${WORK}/classic.save/000.png"; then
    echo "  failed save: notice shown"
else
    echo "FAIL: a failed save showed nothing (or did not fail)"; fail=1
fi
[ "${fail}" -eq 0 ] && echo "save_notice: OK"
exit "${fail}"
