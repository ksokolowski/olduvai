#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# First-run GUI flow — invoked by CTest.
#
# Emulates a double-click launch on a machine with NO config and NO game
# files (fresh XDG_CONFIG_HOME, bare invocation, GUI-launch forced via the
# OLDUVAI_FIRSTRUN_ANSWER hook, dummy SDL drivers so no window opens):
#   quit    → the dialog path exits 1 and writes no config
#   locate  → the forced picker result is validated, persisted to play.json,
#             and the game PLAYS (bounded by --play-frames)
set -eu
# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BIN="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77
[ -f "${GAME_DIR}/FILESA.CUR" ] || { echo "first_run: SKIP — no game data"; exit ${SKIP}; }
[ -x "${BIN}" ] || { echo "first_run: SKIP — no binary"; exit ${SKIP}; }

cfg=$(mktemp -d)
trap 'rm -rf "${cfg}"' EXIT
export XDG_CONFIG_HOME="${cfg}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# 1. quit: exit 1, nothing persisted
if OLDUVAI_FIRSTRUN_ANSWER=quit "${BIN}" </dev/null >/dev/null 2>&1; then
    echo "first_run: FAIL — quit path exited 0"; exit 1
fi
[ ! -f "${cfg}/olduvai/play.json" ] \
    || { echo "first_run: FAIL — quit path wrote a config"; exit 1; }

# 2. locate: picks GAME_DIR, persists it, and the game runs
OLDUVAI_FIRSTRUN_ANSWER=locate OLDUVAI_FIRSTRUN_DIR="${GAME_DIR}" \
    OLDUVAI_FIRSTRUN_PRESET=hd \
    "${BIN}" --play-frames 20 --render-scale 1 </dev/null >/dev/null 2>&1 \
    || { echo "first_run: FAIL — locate path did not play"; exit 1; }
grep -q "game_dir" "${cfg}/olduvai/play.json" \
    || { echo "first_run: FAIL — game_dir not persisted"; exit 1; }
grep -q "widescreen" "${cfg}/olduvai/play.json" \
    || { echo "first_run: FAIL — HD preset not persisted"; exit 1; }

# 3. Session adoption: the SAME run that answered "Enhanced HD" must render
#    enhanced (scale 4 → 1280×800 main-menu shot), not just persist it for
#    the next launch (the "chose Enhanced, got classic DOS" AppImage report).
rm -rf "${cfg}/olduvai"
shot=$(mktemp -u).png
OLDUVAI_FIRSTRUN_ANSWER=locate OLDUVAI_FIRSTRUN_DIR="${GAME_DIR}" \
    OLDUVAI_FIRSTRUN_PRESET=hd OLDUVAI_MAINMENU_SHOT="${shot}" \
    "${BIN}" --play </dev/null >/dev/null 2>&1 || true
[ -s "${shot}" ] \
    || { echo "first_run: FAIL — no main-menu shot from the hd session"; exit 1; }
# PNG IHDR width at offset 16 (big-endian u32): 1280 = 0x00000500.
w=$(od -An -tx1 -j16 -N4 "${shot}" | tr -d ' \n')
rm -f "${shot}"
[ "${w}" = "00000500" ] \
    || { echo "first_run: FAIL — hd choice not adopted in-session (width 0x${w})"; exit 1; }

# 4. Auto-discovered install (game files already complete, no missing-files
#    dialog): the one-time style question must STILL be asked on a GUI
#    launch with a virgin config — and adopted in-session (the GOG
#    silent-classic-DOS field report).  A second launch must NOT re-ask
#    (in headless runs a re-ask would degrade to "dos" and clobber the
#    saved choice — that is exactly the guarded regression).
rm -rf "${cfg}/olduvai"
shot2=$(mktemp -u).png
OLDUVAI_FORCE_GUI=1 OLDUVAI_FIRSTRUN_PRESET=hd OLDUVAI_MAINMENU_SHOT="${shot2}" \
    "${BIN}" --play --game-dir "${GAME_DIR}" </dev/null >/dev/null 2>&1 || true
[ -s "${shot2}" ] \
    || { echo "first_run: FAIL — no shot from the auto-discovered hd session"; exit 1; }
w=$(od -An -tx1 -j16 -N4 "${shot2}" | tr -d ' \n')
rm -f "${shot2}"
[ "${w}" = "00000500" ] \
    || { echo "first_run: FAIL — style ask-once not adopted (width 0x${w})"; exit 1; }
grep -q "widescreen" "${cfg}/olduvai/play.json" \
    || { echo "first_run: FAIL — ask-once choice not persisted"; exit 1; }
OLDUVAI_FORCE_GUI=1 OLDUVAI_MAINMENU_SHOT=/dev/null \
    "${BIN}" --play --game-dir "${GAME_DIR}" </dev/null >/dev/null 2>&1 || true
grep -q "widescreen" "${cfg}/olduvai/play.json" \
    || { echo "first_run: FAIL — second launch re-asked and clobbered the choice"; exit 1; }
echo "first_run: OK — quit + locate + in-session adoption + ask-once behave"
