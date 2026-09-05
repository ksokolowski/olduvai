#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# HD text-screen gate — every (screen x present stack) pair (BACKLOG §3.13,
# §3.14) — invoked by scripts/gate_local.sh, NOT by CTest.
#
# WHAT A "TEXT SCREEN" IS.  The level-entry loading card and the score tally:
# a full-screen upscaled buffer with cartoon vector rows drawn over it at
# OUTPUT resolution.  They share one presenter (TextScreenHd /
# sequence/text_screen_present.hpp) and one handle type, so they share one gate.
#
# WHY ALL FOUR CELLS.  Each screen is reached through TWO present stacks —
# boss_app's and game_app's.  Every defect this gate was built for lived in a
# STACK, not in a screen, so pinning one cell would have missed it from the
# other side.  That is not hypothetical: the tally's logical-size defect
# existed only on the boss path, and the two loading screens ran different
# row-drawing code (draw_centered_overlay_row vs draw_tally_rows_overlay) until
# the presenters were merged.
#
# WHY NOT A CTEST TEST.  The tally rows take ~4 minutes each, almost all of it
# sleeping: the fight must reach the forced win, then the victory sequence,
# fade and tally all run at 18 Hz under SDL_Delay.  Registering that would take
# the suite from 250s to ~500s and make `ctest` a thing people stop running —
# the failure mode §4b spends its length warning about.  It also needs the
# user's game files, so a CI runner could never execute it anyway.
# gate_local.sh is exactly the right home.  (The loading rows are ~1s of work
# each; they ride along here because they gate the same presenter.)
#
# WHAT IT PINS.  text_overlay::flush() RESTORES SDL's logical size from the
# logical_w/logical_h it is handed, so those two ints are not a record of the
# logical size — they are it, for every frame after an overlay draw.  boss_app
# set SDL's logical size for the tally without updating them, and the tally's
# own HD-text flush then put the fight's WIDE dims back one frame later,
# displacing the tally by exactly the pillarbox margin (128 px in an 896x400
# window).  Confirmed by reference: the classic non-widescreen tally lands at
# x[160..402] and so does the fixed widescreen one; the defect put it at
# x[288..530].
#
# Golden = hash of the first 8 renderer-readback frames.  Readback, not an
# offscreen buffer: the defect lives in SDL's logical size and is invisible to
# every native-buffer dump in the tree.  Hash only, never the frames — they are
# decoded game artwork (CONTRIBUTING.md).
#
# Each cell dumps through ITS OWN handle, so the env var names the screen:
# OLDUVAI_DUMP_TALLY and OLDUVAI_DUMP_LOADING.  There is no row-count guessing
# any more — the handle knows which screen it is.  Regenerate after an
# intentional change, then update the .sha256:
#   OLDUVAI_FORCE_WIN=40 OLDUVAI_DUMP_TALLY=<dir> SDL_VIDEODRIVER=dummy \
#   ./build/release/olduvai --play --level 2 --game-dir <game_dir> \
#       --enhanced --hd-profile mmpx --render-scale 2 --aspect widescreen \
#       --window 896x400
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIXDIR="$(dirname "$0")/fixtures"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "hd_text_screens: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "hd_text_screens: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum
    else shasum -a 256; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

# screen | stack | mode | level | seed env (- = none) | timeout
#
# The loading card is the FIRST thing a level presents, so it needs no seed and
# is done in a second — but the run does not end when the dump does (the
# presenter returning false aborts the level, and the title loop then waits for
# input that a dummy video driver never delivers), so the timeout is the exit.
# Frames are on disk long before it fires.
#
# BOTH MODES, because they are different presenters, not one presenter with a
# flag.  `hd` routes through TextScreenHd::present_hd (vector rows at output
# res); `dos` routes through the PresentFn — and the classic path owns code the
# HD path never touches, `tally_pause` among it.  Before these rows existed the
# classic present had NO gate at all, on either stack: BACKLOG §3.14 says as
# much ("a DOS-mode run produces zero frames").
#
# THE LAST COLUMN IS A TIMEOUT, NOT A BUDGET.  The game does not exit after
# dumping a text screen — it plays on with no input — so `timeout` is how each
# cell ends, and every cell therefore costs its FULL column value.  The tally
# rows carried 400 while the frames are all on disk within 30 s, so this script
# spent most of ~15 minutes idling.  90 leaves a 3x margin over the measured
# need.  (Registered as the `hd_text_screens` ctest with label `slow`: the
# release/asan presets filter that label out, `release-full` includes it, and
# gate_local.sh runs it by label.)
# If a cell ever reports fewer than 8 frames, raise ITS number rather than all
# of them, and check whether the dump is genuinely slower or simply not firing.
CASES="tally:boss:hd:2:OLDUVAI_FORCE_WIN=40:90
tally:platform:hd:1:OLDUVAI_FORCE_LEVEL_COMPLETE=8:90
loading:boss:hd:2:-:15
loading:platform:hd:1:-:15
tally:boss:dos:2:OLDUVAI_FORCE_WIN=40:90
tally:platform:dos:1:OLDUVAI_FORCE_LEVEL_COMPLETE=8:90
loading:boss:dos:2:-:15
loading:platform:dos:1:-:15"

rc=0
for cell in ${CASES}; do
    SCREEN="$(echo "${cell}" | cut -d: -f1)"
    STACK="$(echo "${cell}" | cut -d: -f2)"
    MODE="$(echo "${cell}" | cut -d: -f3)"
    LEVEL="$(echo "${cell}" | cut -d: -f4)"
    SEED="$(echo "${cell}" | cut -d: -f5)"
    TMO="$(echo "${cell}" | cut -d: -f6)"
    [ "${SEED}" = "-" ] && SEED=""

    case "${MODE}" in
        hd)  MODE_ARGS="--enhanced --hd-profile mmpx --render-scale 2 --aspect widescreen" ;;
        dos) MODE_ARGS="--profile dos" ;;
        *) echo "hd_text_screens: FAIL — unknown mode '${MODE}'"; exit 2 ;;
    esac

    case "${SCREEN}" in
        tally)   DUMP_VAR=OLDUVAI_DUMP_TALLY ;;
        loading) DUMP_VAR=OLDUVAI_DUMP_LOADING ;;
        *) echo "hd_text_screens: FAIL — unknown screen '${SCREEN}'"; exit 2 ;;
    esac

    DUMP="$(mktemp -d /tmp/olduvai_hdscreen.XXXXXX)"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    # shellcheck disable=SC2086
    env XDG_CONFIG_HOME="${CFG}" ${SEED} "${DUMP_VAR}=${DUMP}" \
        timeout "${TMO}" "${BINARY}" --play --level "${LEVEL}" \
        --game-dir "${GAME_DIR}" ${MODE_ARGS} --window 896x400 \
        >/dev/null 2>&1
    rm -rf "${CFG}"

    LABEL="${SCREEN}/${STACK}/${MODE}"
    n=$(find "${DUMP}" -name "${SCREEN}_*.png" | wc -l | tr -d ' ')
    GOLDEN="${FIXDIR}/hd_screen_${SCREEN}_${STACK}_${MODE}.sha256"
    if [ "${n}" -lt 8 ]; then
        echo "hd_text_screens: FAIL (${LABEL}) — ${n} frames, expected 8; the"
        echo "  screen was not reached.  Check the seed hook still fires."
        rm -rf "${DUMP}"; rc=1; continue
    fi
    GOT=$(cat "${DUMP}"/"${SCREEN}"_*.png | sha256)
    if [ ! -f "${GOLDEN}" ]; then
        echo "hd_text_screens: FAIL (${LABEL}) — no golden at ${GOLDEN} (got ${GOT})"
        rm -rf "${DUMP}"; rc=1; continue
    fi
    if [ "${GOT}" = "$(cat "${GOLDEN}")" ]; then
        echo "hd_text_screens: PASS (${LABEL}, ${n} frames)"
        rm -rf "${DUMP}"
    else
        echo "hd_text_screens: FAIL (${LABEL}) — presented screen differs from golden."
        echo "  frames=${DUMP}  golden=${GOLDEN}   got ${GOT}"
        echo "  A horizontal shift means the logical-size mirror or the row"
        echo "  reservation regressed — see BACKLOG §3.13 / §3.14."
        rc=1
    fi
done
exit ${rc}
