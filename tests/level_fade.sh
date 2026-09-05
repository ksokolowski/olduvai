#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Level-complete fade regression gate — invoked by CTest.
#
# Renders the 8b level-complete fade on BOTH present paths and compares the
# concatenated pre-upscale frames to committed SHA-256 hashes.
#
# WHY THIS EXISTS. Until 2026-07-31 the level-complete intercept had no gate of
# any kind: it is reachable only by finishing a level in a real playthrough, so
# nothing in a 31-test suite touched the fade, the BONUS.MDI hand-off or the
# tally entry. §3.7 slice 2 needed to move 58 lines of it, and moving untested
# code is how a refactor "passes" and ships a defect. The gate came first; the
# extraction was then proved byte-identical against these hashes.
#
# Golden = hash, not image: the frames contain decoded game artwork and the
# content policy (CONTRIBUTING.md) keeps that out of the tree. On failure the
# dumped BMPs are kept for eyeballing and the directory is printed.
#
# Determinism, same reasoning as mainmenu_shot.sh:
#  - OLDUVAI_FORCE_LEVEL_COMPLETE=8 seeds the intercept at a fixed frame, so
#    the fade always starts from the same gameplay frame.
#  - --render-scale 1 / an explicit --window pin the geometry; the defaults are
#    derived from the desktop and vary per host and video driver.
#  - XDG_CONFIG_HOME points at an empty temp dir so the user's play.json
#    (enhanced/hd keys!) cannot leak into either path.
#  - The dump is PRE-upscale, so the HD upscaler — whose float codegen is not
#    relink-stable for omniscale — is not in the hashed data.
#
# Both paths matter and they are different code: `wide` exercises the
# widescreen branch (compose at 320, wrap wide, fade the wide buffer, plus the
# L5 glider overflow re-draw), `classic` the plain 320 present path.
#
# Regenerate after an intentional change, then update the .sha256 files:
#   OLDUVAI_DUMP_LEVEL_FADE=<dir> OLDUVAI_FORCE_LEVEL_COMPLETE=8 \
#   SDL_VIDEODRIVER=dummy ./build/release/olduvai --play --level 1 \
#       --game-dir <game_dir> --render-scale 1 --window 640x400
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIXDIR="$(dirname "$0")/fixtures"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "level_fade: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "level_fade: SKIP — binary not found: ${BINARY}"
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
        FLAGS="--enhanced --aspect widescreen --window 896x400"
    else
        FLAGS="--render-scale 1 --window 640x400"
    fi
    DUMP="$(mktemp -d /tmp/olduvai_levelfade.XXXXXX)"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    # shellcheck disable=SC2086
    XDG_CONFIG_HOME="${CFG}" OLDUVAI_FORCE_LEVEL_COMPLETE=8 \
        OLDUVAI_DUMP_LEVEL_FADE="${DUMP}" timeout 180 \
        "${BINARY}" --play --level 1 --game-dir "${GAME_DIR}" ${FLAGS} \
        >/dev/null 2>&1
    rm -rf "${CFG}"

    n=$(find "${DUMP}" -name 'levelfade_*.bmp' | wc -l | tr -d ' ')
    if [ "${n}" -eq 0 ]; then
        echo "level_fade: FAIL (${mode}) — no frames dumped; the intercept was"
        echo "  never reached.  OLDUVAI_FORCE_LEVEL_COMPLETE wiring?"
        rm -rf "${DUMP}"
        rc=1
        continue
    fi

    GOT=$(cat "${DUMP}"/levelfade_*.bmp | sha256)
    GOLDEN_FILE="${FIXDIR}/level_fade_${mode}.sha256"
    if [ ! -f "${GOLDEN_FILE}" ]; then
        echo "level_fade: FAIL (${mode}) — no golden at ${GOLDEN_FILE}"
        echo "  got ${n} frames, hash ${GOT}"
        rm -rf "${DUMP}"
        rc=1
        continue
    fi
    if [ "${GOT}" = "$(cat "${GOLDEN_FILE}")" ]; then
        echo "level_fade: PASS (${mode}, ${n} frames)"
        rm -rf "${DUMP}"
    else
        echo "level_fade: FAIL (${mode}) — ${n} frames differ from the golden."
        echo "  frames=${DUMP}  golden=${GOLDEN_FILE}"
        echo "  got ${GOT}"
        rc=1
    fi
done
exit ${rc}
