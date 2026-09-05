#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless reinit smoke test — invoked by CTest.
#
# Drives the OLDUVAI_REINIT_TEST env hook: starts level 1 at render_scale=2
# (enhanced/smooth), lets the hook fire on frame 5 (triggers a reinit to
# render_scale=4), then reads the result file and asserts:
#   1. Output width  == 1280  (320 x hd_scale 4, logical pixels)
#   2. Output height == 800   (200 x hd_scale 4)
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
# with a HiDPI scale factor (e.g. a 2x display) that size comes back already
# scaled (2560x1600 for a 1280x800 logical window), which is a display-server
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

# Same reinit again, but FULLSCREEN — a structurally different path since
# 2026-07-27.  Windowed still destroys and recreates the window; fullscreen
# must NOT, because the window is display-sized there and its 320*scale
# dimensions mean nothing, while destroying it costs two macOS Spaces
# animations and seconds of black screen on every enhanced <-> classic switch.
# Only the renderer's logical size and integer-scale flag actually change.
#
# This half was uncovered when the in-place path landed: reinit_smoke drove
# only the windowed branch, so the gate that "covers exactly this path" would
# not have noticed the new one breaking.  State must round-trip identically to
# the windowed run — that is the assertion, not the window size (fullscreen
# reports the DISPLAY size, which legitimately differs).
#
# WHAT THIS DOES NOT COVER, MEASURED.  It asserts the fullscreen path does not
# corrupt STATE, and — since 2026-08-24 (§3.8) — that the post-reinit present
# path IS the requested one (enhanced/smooth stay live on a same-mode reinit;
# a classic switch really leaves both).  What is still not asserted is the
# RENDERED pixels after an in-flight mode switch: the flags are the engine's
# derivation, not SDL's logical size state.  Catching that needs a pixel check
# after an in-flight mode switch, and no hook composes "switch mode, then
# screenshot" today — tracked in docs/internal/BACKLOG.md.  Do not read a
# green reinit_smoke as proof the fullscreen rendering is right.
FS_RESULT_FILE="${RESULT_FILE}.fs"
OLDUVAI_REINIT_TEST="${FS_RESULT_FILE}" \
    "${BINARY}" \
    --play --fullscreen --enhanced --hd-profile smooth --render-scale 2 \
    --game-dir "${GAME_DIR}" \
    --play-frames 240
FS_STATUS=$?
if [ ${FS_STATUS} -ne 0 ]; then
    echo "reinit_smoke: FAIL — fullscreen run exited with status ${FS_STATUS}"
    rm -f "${RESULT_FILE}" "${FS_RESULT_FILE}"
    exit 1
fi

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

# Parse: "out_w out_h pre_x pre_y post_x post_y pre_entcount post_entcount
#          pre_sum post_sum post_enhanced post_smooth post_hd_profile"
# Fields 11+ are §3.8: the live present-path derivation after run_game adopted
# the reinit.  cut (not bare `read`) so appended fields can never spill into
# POST_SUM via the shell's last-variable-takes-the-rest rule.
read -r OUT_W OUT_H PRE_X PRE_Y POST_X POST_Y PRE_ENT POST_ENT PRE_SUM POST_SUM \
    <<< "$(cut -d' ' -f1-10 "${RESULT_FILE}")"
POST_ENH="$(cut -d' ' -f11 "${RESULT_FILE}")"
POST_SMOOTH="$(cut -d' ' -f12 "${RESULT_FILE}")"
rm -f "${RESULT_FILE}"

echo "reinit_smoke: result = ${OUT_W} ${OUT_H} pre=(${PRE_X},${PRE_Y}) post=(${POST_X},${POST_Y}) ent pre=${PRE_ENT} post=${POST_ENT} enh=${POST_ENH} smooth=${POST_SMOOTH}"

FAIL=0

# §3.8: the same-mode reinit must still be on the ENHANCED present path with
# smooth motion live — the derivation the shipped bug left stale.
if [ "${POST_ENH}" != "1" ] || [ "${POST_SMOOTH}" != "1" ]; then
    echo "reinit_smoke: FAIL — post-reinit present path wrong: enhanced=${POST_ENH} smooth=${POST_SMOOTH} (expected 1/1)"
    FAIL=1
fi

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

# The fullscreen run must preserve state exactly as the windowed one does.
# Fields 3-10: positions/entities/checksums only — window dimensions
# legitimately differ (fullscreen reports the display size; windowed reports
# 320*scale x 200*scale), and the §3.8 present-path flags are asserted per run.
FS_STATE="$(cut -d' ' -f3-10 "${FS_RESULT_FILE}" 2>/dev/null)"
WIN_STATE="${PRE_X} ${PRE_Y} ${POST_X} ${POST_Y} ${PRE_ENT} ${POST_ENT} ${PRE_SUM} ${POST_SUM}"
if [ -z "${FS_STATE}" ]; then
    echo "reinit_smoke: FAIL — fullscreen run produced no result line"
    FAIL=1
elif [ "${FS_STATE}" != "${WIN_STATE}" ]; then
    echo "reinit_smoke: FAIL — fullscreen reinit did not preserve state like windowed"
    echo "  windowed:   ${WIN_STATE}"
    echo "  fullscreen: ${FS_STATE}"
    FAIL=1
fi
FS_ENH="$(cut -d' ' -f11 "${FS_RESULT_FILE}" 2>/dev/null)"
FS_SMOOTH="$(cut -d' ' -f12 "${FS_RESULT_FILE}" 2>/dev/null)"
if [ "${FS_ENH}" != "1" ] || [ "${FS_SMOOTH}" != "1" ]; then
    echo "reinit_smoke: FAIL — fullscreen post-reinit present path wrong: enhanced=${FS_ENH} smooth=${FS_SMOOTH}"
    FAIL=1
fi
rm -f "${FS_RESULT_FILE}"

# ── Third path (§3.8): the ENHANCED -> CLASSIC switch. ──────────────────
# The shipped regression ran in exactly this direction: a session started
# --profile hd kept smooth motion after switching to Classic, because the
# derived field was never re-adopted.  State-only assertions passed straight
# through it; the present-path flags are what catch it now.  The stale
# hd_profile STRING in the report is expected residue (enhanced=0 gates every
# consumer); the flags are the contract.
CLS_RESULT_FILE="$(mktemp /tmp/reinit_cls.XXXXXX)"
OLDUVAI_REINIT_TEST="${CLS_RESULT_FILE}" OLDUVAI_REINIT_CLASSIC=1 \
    "${BINARY}" \
    --play --enhanced --hd-profile smooth --render-scale 2 \
    --game-dir "${GAME_DIR}" \
    --play-frames 240
CLS_STATUS=$?
CLS_LINE="$(cut -d' ' -f1-12 "${CLS_RESULT_FILE}" 2>/dev/null)"
rm -f "${CLS_RESULT_FILE}"
if [ ${CLS_STATUS} -ne 0 ] || [ -z "${CLS_LINE}" ]; then
    echo "reinit_smoke: FAIL — classic-switch run exited ${CLS_STATUS} or wrote no result"
    FAIL=1
else
    CLS_STATE="$(printf '%s\n' "${CLS_LINE}" | cut -d' ' -f3-10)"
    if [ "${CLS_STATE}" != "${WIN_STATE}" ]; then
        echo "reinit_smoke: FAIL — classic switch did not preserve state like the enhanced runs"
        echo "  enhanced: ${WIN_STATE}"
        echo "  classic:  ${CLS_STATE}"
        FAIL=1
    fi
    CLS_ENH="$(printf '%s\n' "${CLS_LINE}" | cut -d' ' -f11)"
    CLS_SMOOTH="$(printf '%s\n' "${CLS_LINE}" | cut -d' ' -f12)"
    if [ "${CLS_ENH}" != "0" ] || [ "${CLS_SMOOTH}" != "0" ]; then
        echo "reinit_smoke: FAIL — classic switch left the present path enhanced: enhanced=${CLS_ENH} smooth=${CLS_SMOOTH}"
        FAIL=1
    else
        echo "reinit_smoke: classic switch — present path is classic, state preserved"
    fi
fi

# ── Fourth path (2026-08-24): CLASSIC/DOS -> ENHANCED adoption. ─────────
# The reverse of the shipped bug's direction, raised by playtest: does a
# dos-started session gain WORKING smooth motion when the menu switches to
# enhanced?  Flags must land at 1/1 with state preserved.  (hd_profile rides
# along as the dos value here — the hook copies opts; the menu's Style
# presets set their own.)
ENH_RESULT_FILE="$(mktemp /tmp/reinit_enh.XXXXXX)"
OLDUVAI_REINIT_TEST="${ENH_RESULT_FILE}" OLDUVAI_REINIT_ENHANCED=1 \
    "${BINARY}" \
    --play --profile dos \
    --game-dir "${GAME_DIR}" \
    --play-frames 240
ENH_STATUS=$?
ENH_LINE="$(cut -d' ' -f1-12 "${ENH_RESULT_FILE}" 2>/dev/null)"
rm -f "${ENH_RESULT_FILE}"
if [ ${ENH_STATUS} -ne 0 ] || [ -z "${ENH_LINE}" ]; then
    echo "reinit_smoke: FAIL — to-enhanced run exited ${ENH_STATUS} or wrote no result"
    FAIL=1
else
    # Within-run round trip only: this scenario STARTS under a different
    # config than the enhanced runs above, and the intro overlay's wall-clock
    # dwell shifts which logic tick 'frame 5' lands on (BACKLOG §6's known
    # --play-shot-frame nondeterminism).  Cross-RUN equality would compare
    # different spawn phases; the reinit contract is that PRE==POST inside
    # one run, which fields 3-6 encode.
    ENH_PRE="$(printf '%s\n' "${ENH_LINE}" | cut -d' ' -f3-4)"
    ENH_POST="$(printf '%s\n' "${ENH_LINE}" | cut -d' ' -f5-6)"
    ENH_ENT="$(printf '%s\n' "${ENH_LINE}" | cut -d' ' -f7-10)"
    if [ "${ENH_PRE}" != "${ENH_POST}" ] || [ -z "${ENH_ENT}" ]; then
        echo "reinit_smoke: FAIL — classic->enhanced switch did not preserve state"
        echo "  pre=(${ENH_PRE}) post=(${ENH_POST}) ent=${ENH_ENT}"
        FAIL=1
    fi
    ENH_FLAG="$(printf '%s\n' "${ENH_LINE}" | cut -d' ' -f11)-$(printf '%s\n' "${ENH_LINE}" | cut -d' ' -f12)"
    if [ "${ENH_FLAG}" != "1-1" ]; then
        echo "reinit_smoke: FAIL — classic->enhanced left present path wrong: ${ENH_FLAG} (expected 1-1)"
        FAIL=1
    else
        echo "reinit_smoke: classic->enhanced — smooth live, state preserved"
    fi
fi

if [ ${FAIL} -eq 0 ]; then
    echo "reinit_smoke: PASS (windowed + fullscreen + classic switch + enhanced adoption)"
fi

exit ${FAIL}
