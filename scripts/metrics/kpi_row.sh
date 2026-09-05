#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The §5 KPI complexity figures, MEASURED — and a --check that fails on drift.
#
# WHY THIS EXISTS.  §5 opens by warning that its table "goes stale the moment a
# commit lands without touching it".  It was right, five times: the row was
# found stale at 766/667 against a real 728/654, six commits behind at 728,
# and — while this script was being written — §5's headline still read 728
# against a real 630, inside the same table whose prose calls itself "the table
# to trust".  Every occurrence had the identical shape: a hand-TYPED number
# claiming to be a MEASUREMENT, in a repo where the measuring tool is already
# installed and wired to a compile database.
#
# So stop typing it.
#
#   scripts/metrics/kpi_row.sh            # print the current figures
#   scripts/metrics/kpi_row.sh --check    # exit 1 if BACKLOG §5 disagrees
#   scripts/metrics/kpi_row.sh --tail     # print the §3.12 tail rows too
#
# --check is the point: drift becomes a detectable condition instead of
# something a reader has to notice.  It is deliberately NOT a ctest gate — the
# numbers are supposed to fall, and a test that fails on improvement would be
# gamed into uselessness within a week.  Run it before writing a KPI row.
#
# --tail exists because the 2026-08-23 read found `main` at 138 against a
# recorded 93 — +45 of feature growth that nothing was positioned to see,
# because --check guards only §5's four drivers.  It prints the named §3.12
# rows next to the drivers so the periodic read is one command; it does NOT
# gate them, honouring §3.12's "periodic read, not a new gate".  Reconcile
# §3.12 against its output (RELEASE_CUT_RUNBOOK pre-flight carries the step).
#
# WHERE THIS IS WRONG.  It reports what clang-tidy's
# readability-function-cognitive-complexity computes for the four §5-tracked
# drivers (--tail: plus the named §3.12 rows) and nothing else; a function
# renamed or moved to a new file silently drops out of the list rather than
# erroring, and the tables have other rows (test counts, fanout, slurp
# definitions) this does not touch.  The §4 anti-list is deliberately absent
# from TAIL: mmpx_2x / omniscale are never to be chased.  It also needs a
# configured build/release compile database — without one it skips (77)
# rather than guessing.

set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SKIP=77
BUILD="${ROOT}/build/release"
BACKLOG="${ROOT}/docs/internal/BACKLOG.md"

TIDY="$(command -v clang-tidy 2>/dev/null || true)"
[ -z "${TIDY}" ] && [ -x "${HOME}/.local/bin/clang-tidy" ] && TIDY="${HOME}/.local/bin/clang-tidy"
if [ -z "${TIDY}" ]; then
    echo "kpi_row: SKIP — clang-tidy not found"
    exit ${SKIP}
fi
if [ ! -f "${BUILD}/compile_commands.json" ]; then
    echo "kpi_row: SKIP — no compile database at ${BUILD} (cmake --preset release)"
    exit ${SKIP}
fi

SDK="$(xcrun --show-sdk-path 2>/dev/null || true)"
SYSROOT=""
[ -n "${SDK}" ] && SYSROOT="--extra-arg=-isysroot${SDK}"

# function:file — the four complexity rows §5 tracks.
TRACKED="run_platform_level:src/presentation/game_app.cpp
run_game:src/presentation/game_app.cpp
run_boss_level:src/presentation/boss_app.cpp
run_title_menu:src/presentation/title_menu_flow.cpp"

# function:file — the named §3.12 tail rows (2026-08-23 read).  Values move;
# §3.12 is the doc to reconcile, this is only the measurement half.
TAIL="parse_args:src/app/cli_args.cpp
main:src/app/main.cpp
play_panorama_wide:src/presentation/sequence/transition_players.cpp
play_transition:src/presentation/sequence/transition_players.cpp
play_transition_wide:src/presentation/sequence/transition_players.cpp
resolve_and_bake_sfx:src/presentation/audio/audio.cpp
build_screen_tiles:src/presentation/render/screen_tiles.cpp
show_win_ending:src/presentation/sequence/end_sequence.cpp
bind_screen:src/presentation/level/level_setup.cpp
load_level_impl:src/presentation/level/level_setup.cpp
parse_mdi_events:src/formats/mdi.cpp
build_gm_midi:src/formats/mdi.cpp
compose_static_wide_bg_native:src/presentation/render/bg_compose.cpp
compose_widescreen:src/presentation/render/widescreen.cpp
draw_menu_vector:src/presentation/menu/menu_render.cpp
run_viewer:src/presentation/diag/viewer.cpp
update_boss_player:src/systems/boss.cpp
update_projectiles:src/systems/boss_l2.cpp
process_entity_collisions:src/systems/collision_dispatch.cpp
scale3x:src/enhance/pixel_scalers.cpp"

measure() {   # measure <fn> <file> -> complexity, or "?" if not reported
    # shellcheck disable=SC2086
    "${TIDY}" -p "${BUILD}" --quiet ${SYSROOT} \
        --checks='-*,readability-function-cognitive-complexity' \
        --config="{CheckOptions:[{key: readability-function-cognitive-complexity.Threshold, value: '1'}]}" \
        "${ROOT}/$2" 2>/dev/null |
      sed -n "s/.*warning: function '$1' has cognitive complexity of \([0-9]*\).*/\1/p" |
      head -1
}

FAIL=0
OUT=""
for spec in ${TRACKED}; do
    fn="${spec%%:*}"; file="${spec#*:}"
    n="$(measure "${fn}" "${file}")"
    [ -z "${n}" ] && n="?"
    OUT="${OUT}${fn} ${n}\n"
done

if [ "$1" = "--check" ]; then
    # Compare against the §5 headline cell for each tracked function.  The row
    # shape is:  | `fn` ... | a | b | **N** | note |
    printf '%b' "${OUT}" | while read -r fn n; do
        [ "${n}" = "?" ] && continue
        # ONLY inside "## 5. KPIs to watch" — the first attempt grepped the
        # whole file, matched §3's diagnosis table instead, and cheerfully
        # reported agreement while §5 was 98 points stale.  A checker that
        # reads the wrong row is the very defect it was written to end.
        row="$(awk '/^## 5\. KPIs to watch/{s=1} s&&/^## [0-9]/&&!/^## 5\./{exit} s' \
                "${BACKLOG}" | grep -m1 "^| \`${fn}\`" || true)"
        [ -z "${row}" ] && continue
        # The HEADLINE cell (5th pipe field), not "the last bold number on the
        # line" — attempt two did that and matched the NOTE column, whose
        # history tail ends with the current figure by construction, so it
        # agreed with itself forever.
        claimed="$(printf '%s' "${row}" | awk -F'|' '{print $5}' |
                   sed -n 's/[^0-9]*\([0-9][0-9]*\).*/\1/p')"
        [ -z "${claimed}" ] && continue
        if [ "${claimed}" != "${n}" ]; then
            echo "kpi_row: DRIFT — ${fn}: BACKLOG says ${claimed}, measured ${n}"
            echo "DRIFT" >> /tmp/kpi_row_drift.$$
        fi
    done
    if [ -f "/tmp/kpi_row_drift.$$" ]; then
        rm -f "/tmp/kpi_row_drift.$$"
        echo "kpi_row: the §5 table is stale — update it with the figures below."
        FAIL=1
    else
        echo "kpi_row: §5 matches the measurement."
    fi
fi

if [ "$1" = "--tail" ]; then
    for spec in ${TAIL}; do
        fn="${spec%%:*}"; file="${spec#*:}"
        n="$(measure "${fn}" "${file}")"
        [ -z "${n}" ] && n="?"
        OUT="${OUT}${fn} ${n}\n"
    done
    echo "── cognitive complexity at $(git -C "${ROOT}" rev-parse --short HEAD) (§5 drivers + §3.12 tail) ──"
    printf '%b' "${OUT}" | while read -r fn n; do
        printf '  %-28s %s\n' "${fn}" "${n}"
    done
    exit 0
fi

echo "── cognitive complexity at $(git -C "${ROOT}" rev-parse --short HEAD) ──"
printf '%b' "${OUT}" | while read -r fn n; do
    printf '  %-22s %s\n' "${fn}" "${n}"
done
exit ${FAIL}
