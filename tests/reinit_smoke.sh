#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless reinit smoke test — invoked by CTest.
#
# Drives the OLDUVAI_REINIT_TEST env hook: starts level 1 at render_scale=2
# (enhanced/smooth), lets the hook fire on frame 5 (triggers a reinit to
# render_scale=4), then reads the result file and asserts:
#   1. Output width  == 1280  (320 × hd_scale 4, logical pixels)
#   2. Output height == 800   (200 × hd_scale 4)
#   3. Pre-reinit player x == post-reinit player x  (state preserved)
#   4. Pre-reinit player y == post-reinit player y
#
# Skip conditions (exits 77 = CTest SKIP_RETURN_CODE):
#   • game data absent (CI without data must not fail)
#   • SDL cannot open a display  (SDL_VIDEODRIVER=dummy route works locally;
#     on a truly headless CI the binary will fail to init — the data-absent
#     guard normally covers that case first)

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
RESULT_FILE="$(mktemp /tmp/reinit_result.XXXXXX)"
SKIP=77

# Guard: skip when game data is absent (CI without assets).
if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "reinit_smoke: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    rm -f "${RESULT_FILE}"
    exit ${SKIP}
fi

# Guard: skip when the binary does not exist (shouldn't happen in a CTest run
# since it's a dependency, but be defensive).
if [ ! -x "${BINARY}" ]; then
    echo "reinit_smoke: SKIP — binary not found: ${BINARY}"
    rm -f "${RESULT_FILE}"
    exit ${SKIP}
fi

# Pin to SDL's dummy video driver unless the caller overrode it.  The reinit
# hook reports the LOGICAL window size (SDL_GetWindowSize); on a real desktop
# with a HiDPI scale factor (e.g. a 2× display) that size comes back already
# scaled (2560×1600 for a 1280×800 logical window), which is a display-server
# artifact, not an engine result.  The dummy driver has no DPI scaling, so the
# assertions test the reinit logic deterministically on any machine.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"   # mute test runs

# Run the game with the reinit hook.  --play-frames 240 is a safety ceiling
# (the hook fires on frame 5 and exits on re-entry frame 0, well under 240).
OLDUVAI_REINIT_TEST="${RESULT_FILE}" \
    "${BINARY}" \
    --play --enhanced --hd-profile smooth --render-scale 2 \
    --game-dir "${GAME_DIR}" \
    --play-frames 240
STATUS=$?

if [ ${STATUS} -ne 0 ]; then
    echo "reinit_smoke: FAIL — binary exited with status ${STATUS}"
    rm -f "${RESULT_FILE}"
    exit 1
fi

if [ ! -f "${RESULT_FILE}" ] || [ ! -s "${RESULT_FILE}" ]; then
    echo "reinit_smoke: FAIL — result file not written"
    rm -f "${RESULT_FILE}"
    exit 1
fi

# Parse: "out_w out_h pre_x pre_y post_x post_y pre_entcount post_entcount"
read -r OUT_W OUT_H PRE_X PRE_Y POST_X POST_Y PRE_ENT POST_ENT PRE_SUM POST_SUM < "${RESULT_FILE}"
rm -f "${RESULT_FILE}"

echo "reinit_smoke: result = ${OUT_W} ${OUT_H} pre=(${PRE_X},${PRE_Y}) post=(${POST_X},${POST_Y}) ent pre=${PRE_ENT} post=${POST_ENT}"

FAIL=0

if [ "${OUT_W}" != "1280" ]; then
    echo "reinit_smoke: FAIL — expected width 1280, got ${OUT_W}"
    FAIL=1
fi

if [ "${OUT_H}" != "800" ]; then
    echo "reinit_smoke: FAIL — expected height 800, got ${OUT_H}"
    FAIL=1
fi

if [ "${PRE_X}" != "${POST_X}" ]; then
    echo "reinit_smoke: FAIL — player x not preserved: pre=${PRE_X} post=${POST_X}"
    FAIL=1
fi

if [ "${PRE_Y}" != "${POST_Y}" ]; then
    echo "reinit_smoke: FAIL — player y not preserved: pre=${PRE_Y} post=${POST_Y}"
    FAIL=1
fi

# Full-state round-trip: the live screen's entities (e.g. the L1 screen-0 spike)
# must survive the save→reinit→restore — not just the player.
if [ "${PRE_ENT}" = "0" ] || [ -z "${PRE_ENT}" ]; then
    echo "reinit_smoke: FAIL — pre-reinit entity count is 0/empty (expected >0 on L1 screen 0)"
    FAIL=1
fi

if [ "${PRE_ENT}" != "${POST_ENT}" ]; then
    echo "reinit_smoke: FAIL — entities not preserved across reinit: pre=${PRE_ENT} post=${POST_ENT}"
    FAIL=1
fi

# CONTENT checksum (type,x,y,state,counter,ko_counter,active per entity):
# an equal count of freshly-RESET entities must not pass — same count with
# reset state is the historic false-confidence shape (count-only check).
if [ "${PRE_SUM}" != "${POST_SUM}" ]; then
    echo "reinit_smoke: FAIL — entity content changed across reinit: pre=${PRE_SUM} post=${POST_SUM}"
    FAIL=1
fi

if [ ${FAIL} -eq 0 ]; then
    echo "reinit_smoke: PASS"
fi

exit ${FAIL}
