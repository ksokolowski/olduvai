#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The F5 bug report written from a boss arena — invoked by CTest.
#
# WHY THIS EXISTS.  The boss report was a thinner copy of the platform one
# (BACKLOG §3.30): no report form, no Display section, no boss state, and a
# synthesised SystemsState whose zeros (food, timer, screen) read as data.
# Step 1 added the Display section, a "Boss fight" section and n/a for the
# platform-only rows; step 2 put the platform's F5 form in the boss arena.
# OLDUVAI_MENU_SCRIPT walks the form exactly as tests/report_form.sh does on
# a platform level — 80 idle frames, F5, tag -> visual, a typed description,
# Save — and this checks what reached the report, in classic and in enhanced
# widescreen.
#
# The last part walks the boss pause's Quit -> Exit Game confirm (No, Yes).
#
# It also sets OLDUVAI_DUMP_OUTPUT: before §3.32 no boss present called the
# final-output dump hook, so a boss run dumped nothing at all.
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_report: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_report: SKIP — binary not found: ${BINARY}"
    exit 77
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

# 80 x wait, then the platform save walk minus its shots.
WALK="$(i=0; while [ $i -lt 80 ]; do printf 'wait '; i=$((i + 1)); done)"
WALK="${WALK}f5 right down down enter type:boss_bug_here ctrlenter esc enter wait"

rc=0
for mode in classic wide; do
    if [ "${mode}" = wide ]; then
        FLAGS="--profile hd --aspect widescreen --window 896x400"
        WANT_WS="| Widescreen active | YES |"
    else
        FLAGS="--profile dos --render-scale 1 --window 640x400"
        WANT_WS="| Widescreen active | NO |"
    fi
    WORK="$(mktemp -d /tmp/olduvai_bossreport.XXXXXX)"
    mkdir "${WORK}/cfg" "${WORK}/bugs" "${WORK}/out"
    # shellcheck disable=SC2086
    XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_BUG_DIR="${WORK}/bugs" \
        OLDUVAI_MENU_SCRIPT="${WALK}" OLDUVAI_DUMP_OUTPUT="${WORK}/out" \
        "${BINARY}" --play --level 2 --play-frames 90 \
        --game-dir "${GAME_DIR}" ${FLAGS} >"${WORK}/run.log" 2>&1
    report=$(find "${WORK}/bugs" -name report.md | head -n 1)
    fail=""
    if [ -z "${report}" ]; then
        fail="no report written"
    else
        dir=$(dirname "${report}")
        for want in "**Tag:** visual" "boss bug here" \
                    "## Boss fight" "| Boss health | " "| Phase | jaw " \
                    "| Fight frame | 80 |" "## Display" "${WANT_WS}" \
                    "| Food | n/a (boss arena) |" "| Score | "; do
            grep -qF -- "${want}" "${report}" || fail="${fail} missing '${want}';"
        done
        grep -qF "screenshot_collision" "${report}" &&
            fail="${fail} lists the platform-only overlays;"
        [ -f "${dir}/screenshot_collision.png" ] &&
            fail="${fail} wrote screenshot_collision.png;"
        [ -f "${dir}/screenshot.png" ] || fail="${fail} no screenshot.png;"
    fi
    ndump=$(find "${WORK}/out" -name 'out_*.bmp' | wc -l | tr -d ' ')
    [ "${ndump}" -gt 80 ] ||
        fail="${fail} OLDUVAI_DUMP_OUTPUT wrote ${ndump} boss frames (want > 80);"
    if [ -n "${fail}" ]; then
        echo "boss_report: FAIL (${mode}) —${fail}"
        echo "  kept: ${WORK}"
        rc=1
    else
        echo "boss_report: PASS (${mode}, ${ndump} frames dumped)"
        rm -rf "${WORK}"
    fi
done

# The boss pause's Quit (spec 2026-09-18): Quit -> Exit Game asks first.  The
# end of a menu script also ends the run, so "did Yes exit?" is read from a
# side effect instead: an F5 save walk AFTER the answer writes a report only
# if the fight is still running.  Answer No -> a report (the control: the
# tail walk works); answer Yes -> none.
TAIL="wait wait f5 right down down enter type:after ctrlenter esc enter wait"
for answer in no yes; do
    if [ "${answer}" = yes ]; then KEYS="right enter"; else KEYS="enter esc esc"; fi
    WORK="$(mktemp -d /tmp/olduvai_bossquit.XXXXXX)"
    mkdir "${WORK}/cfg" "${WORK}/bugs"
    XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_BUG_DIR="${WORK}/bugs" \
        OLDUVAI_MENU_SCRIPT="wait wait esc down down down enter down enter ${KEYS} ${TAIL}" \
        "${BINARY}" --play --level 2 --game-dir "${GAME_DIR}" \
        --profile dos --render-scale 1 --window 640x400 >"${WORK}/run.log" 2>&1
    brc=$?
    report=$(find "${WORK}/bugs" -name report.md | head -n 1)
    if [ ${brc} -ne 0 ]; then
        echo "boss_report: FAIL (quit ${answer}) — exit status ${brc}"; rc=1
    elif [ "${answer}" = no ] && [ -z "${report}" ]; then
        echo "boss_report: FAIL (quit no) — the fight did not continue after No"; rc=1
    elif [ "${answer}" = yes ] && [ -n "${report}" ]; then
        echo "boss_report: FAIL (quit yes) — the fight kept running after Yes"; rc=1
    else
        echo "boss_report: PASS (quit ${answer})"
        rm -rf "${WORK}"
        continue
    fi
    echo "  kept: ${WORK}"
done
exit ${rc}
