#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# F5 bug-report form — headless regression gate (invoked by CTest).
#
# Drives the in-engine bug-report form with OLDUVAI_MENU_SCRIPT (synthetic
# SDL key + text events, one token per frame): open via F5, navigate the
# tag/repro choice rows, open the multi-line description editor, type + edit,
# save through the confirm dialog.  Two checks:
#   1. golden shots (form + editor) SHA-256 vs the committed fixtures;
#   2. the SAVED report actually carries the annotations (state.json +
#      report.md) — the functional guarantee the shots can't see.
# A second walk proves Discard writes nothing.
#
# Determinism: --render-scale 1 (integer path) + --window 640x400 pin the
# output; XDG_CONFIG_HOME + OLDUVAI_BUG_DIR isolate config and the report
# output (walk 4 instead overrides HOME to prove the default root).
# Skip (77) when game data or the binary is absent.
set -eu

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
GAME_DIR=$(cd "${GAME_DIR}" 2>/dev/null && pwd || echo "${GAME_DIR}")
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
case "${BINARY}" in /*) ;; *) BINARY="$(pwd)/${BINARY}" ;; esac
FIX="$(cd "$(dirname "$0")/fixtures" && pwd)"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "report_form: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "report_form: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
FAIL=0

WORK=$(mktemp -d /tmp/report_form.XXXXXX)
trap 'rm -rf "${WORK}"' EXIT

run() {  # run <name> <script>  → shots in ${WORK}/<name>, bug_reports in ${WORK}
    OUT="${WORK}/$1"
    mkdir -p "${OUT}"
    ( cd "${WORK}" &&
      XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_BUG_DIR="${WORK}/bug_reports" \
      OLDUVAI_MENU_SCRIPT="$2" \
      OLDUVAI_MENU_SCRIPT_DIR="${OUT}" timeout 60 \
      "${BINARY}" --play --level 1 --render-scale 1 --window 640x400 \
      --game-dir "${GAME_DIR}" >/dev/null 2>&1 )
}

run_hd() {  # run_hd <name> <script>  → HD (×4) run so the presented shot exists
    OUT="${WORK}/$1"
    mkdir -p "${OUT}"
    ( cd "${WORK}" &&
      XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_BUG_DIR="${WORK}/bug_reports" \
      OLDUVAI_MENU_SCRIPT="$2" \
      OLDUVAI_MENU_SCRIPT_DIR="${OUT}" timeout 60 \
      "${BINARY}" --play --level 1 --render-scale 4 --enhanced --window 1280x800 \
      --game-dir "${GAME_DIR}" >/dev/null 2>&1 )
}

check_golden() {  # check_golden <name>  → compare ${WORK}/<name>/*.png to fixtures
    GOLDEN="${FIX}/$1.sha256"
    [ -f "${GOLDEN}" ] || { echo "report_form[$1]: FAIL — no golden ${GOLDEN}"; FAIL=1; return; }
    while read -r WANT NAME; do
        [ -n "${NAME}" ] || continue
        got="${WORK}/$1/${NAME}"
        if [ ! -s "${got}" ]; then
            echo "report_form[$1]: FAIL — shot ${NAME} not produced"; FAIL=1
        elif [ "$(sha256 "${got}")" != "${WANT}" ]; then
            echo "report_form[$1]: FAIL — ${NAME} differs from golden"; FAIL=1
        fi
    done < "${GOLDEN}"
}

# ── 1. save walk: open, tag→visual, edit description (type + newline), save ──
rm -rf "${WORK}/bug_reports"
run report_save \
    "wait wait f5 shot right shot down down enter shot type:it_broke_here enter type:step_two ctrlenter shot esc shot enter"
check_golden report_save

saved=$(ls -dt "${WORK}"/bug_reports/*/ 2>/dev/null | head -1)
if [ -z "${saved}" ]; then
    echo "report_form: FAIL — Save wrote no report"; FAIL=1
else
    grep -q '\*\*Tag:\*\* visual' "${saved}report.md" \
        || { echo "report_form: FAIL — tag not in report.md"; FAIL=1; }
    grep -q 'it broke here' "${saved}report.md" \
        || { echo "report_form: FAIL — description not in report.md"; FAIL=1; }
    [ ! -e "${saved}state.json" ] \
        || { echo "report_form: FAIL — state.json should no longer be written"; FAIL=1; }
    # Classic 1× has no HD/widescreen transform, so no presented shot is written
    # (the native screenshot.png is pixel-equal to it).
    [ ! -e "${saved}screenshot_presented.png" ] \
        || { echo "report_form: FAIL — classic run should write no presented shot"; FAIL=1; }
    grep -q 'screenshot_presented.png' "${saved}report.md" \
        && { echo "report_form: FAIL — classic report.md should not list presented shot"; FAIL=1; }
fi

# ── 2. discard walk: open then discard → nothing new written ──
rm -rf "${WORK}/bug_reports"
run report_discard "wait wait f5 esc right enter"
if [ -n "$(ls -d "${WORK}"/bug_reports/*/ 2>/dev/null)" ]; then
    echo "report_form: FAIL — Discard wrote a report"; FAIL=1
fi

# ── 3. default root: no OLDUVAI_BUG_DIR → <home>/olduvai/bug_reports ──
# (HOME points into the workdir so the tester's real home stays untouched.)
OUT="${WORK}/report_home"
mkdir -p "${OUT}"
( cd "${WORK}" &&
  HOME="${WORK}/home" XDG_CONFIG_HOME="${WORK}/cfg" \
  OLDUVAI_MENU_SCRIPT="wait wait f5 esc enter" \
  OLDUVAI_MENU_SCRIPT_DIR="${OUT}" timeout 60 \
  "${BINARY}" --play --level 1 --render-scale 1 --window 640x400 \
  --game-dir "${GAME_DIR}" >/dev/null 2>&1 )
home_saved=$(ls -d "${WORK}"/home/olduvai/bug_reports/*/ 2>/dev/null | head -1)
if [ -z "${home_saved}" ]; then
    echo "report_form: FAIL — default root wrote nothing under \$HOME/olduvai/bug_reports"; FAIL=1
elif [ ! -s "${home_saved}report.md" ]; then
    echo "report_form: FAIL — default-root report.md missing"; FAIL=1
fi
if [ -n "$(ls -d "${WORK}"/bug_reports/*/ 2>/dev/null)" ]; then
    echo "report_form: FAIL — default-root walk leaked into cwd bug_reports/"; FAIL=1
fi

# ── 4. HD walk: the presented shot IS written at output resolution ──
# (What the player actually saw: HD upscale + HUD.  Pixels are intentionally
# non-deterministic across builds, so assert existence + dimensions, not SHA.)
rm -rf "${WORK}/bug_reports"
run_hd report_hd "wait wait f5 right down down enter type:hd ctrlenter esc enter"
hd_saved=$(ls -dt "${WORK}"/bug_reports/*/ 2>/dev/null | head -1)
if [ -z "${hd_saved}" ]; then
    echo "report_form: FAIL — HD walk wrote no report"; FAIL=1
elif [ ! -s "${hd_saved}screenshot_presented.png" ]; then
    echo "report_form: FAIL — HD run wrote no presented shot"; FAIL=1
else
    grep -q 'screenshot_presented.png' "${hd_saved}report.md" \
        || { echo "report_form: FAIL — HD report.md should list presented shot"; FAIL=1; }
    if command -v identify >/dev/null 2>&1; then
        dims=$(identify -format '%wx%h' "${hd_saved}screenshot_presented.png" 2>/dev/null || echo "?")
        [ "${dims}" = "1280x800" ] \
            || { echo "report_form: FAIL — presented shot is ${dims}, want 1280x800"; FAIL=1; }
    fi
fi

if [ ${FAIL} -eq 0 ]; then
    echo "report_form: OK"
fi
exit ${FAIL}
