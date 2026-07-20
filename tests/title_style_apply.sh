#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Title-screen Style Apply equivalence gate — invoked by CTest.
#
# Applying the Enhanced HD Style preset AT THE TITLE SCREEN must leave the
# title menu rendering EXACTLY as a session that booted enhanced from config
# would render it — same rebuilt window, same upscale, same vector glyphs.
# The historical failure mode: the apply reinit rebuilt the window and
# reloaded the HD font, but the menu kept a stale pre-apply render decision
# (latched menu_use_vector) and drew classic bitmap glyphs until Start Game.
#
# Two runs through the title-menu OLDUVAI_MENU_SCRIPT walk, SHARED config dir:
#   run 1 (fresh config → boots classic):
#     down down enter   → main cursor to Options, open it (cursor on Style)
#     right             → Style: Classic DOS → Enhanced HD (preset fan-out)
#     esc               → leave Options; staged changes open the confirm dialog
#     enter             → Apply (reinit-class: in-place window/font rebuild),
#                         and persist the preset to play.json
#     wait wait shot    → settle two frames, dump 000.png
#   run 2 (SAME config → boots enhanced from the persisted play.json):
#     down down enter esc → identical navigation (Options and back; nothing
#                           staged, no dialog), so the cursor state matches
#     wait wait shot      → dump 000.png
#
# The two shots must be byte-identical.  No goldens: post-apply frames go
# through the float omniscale x4 upscaler, whose codegen reorders between
# toolchains (see mainmenu_shot.sh) — but BOTH runs use the same binary, so
# equivalence is exact by construction.  This also covers the persist side
# (run 2 only boots enhanced if run 1's Apply actually wrote play.json).
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data or the binary is absent.

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "title_style_apply: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "title_style_apply: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

# sha256 <file> — portable (Linux sha256sum / macOS shasum).
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"   # mute test runs

CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
OUT1="$(mktemp -d /tmp/title_style1.XXXXXX)"
OUT2="$(mktemp -d /tmp/title_style2.XXXXXX)"
cleanup() { rm -rf "${CFG_DIR}" "${OUT1}" "${OUT2}"; }

# NO --window / --render-scale: the apply rebuilds the window at the preset's
# scale, and run 2 must size itself from the persisted config the same way.
run_walk() {
    XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_MENU_SCRIPT="$1" \
        OLDUVAI_MENU_SCRIPT_DIR="$2" timeout 60 \
        "${BINARY}" --play --game-dir "${GAME_DIR}" >/dev/null 2>&1
}

run_walk "down down enter right esc enter wait wait shot quit" "${OUT1}"
if [ ! -s "${OUT1}/000.png" ]; then
    echo "title_style_apply: FAIL — no shot from the apply walk (menu not reached?)"
    cleanup; exit 1
fi
if [ ! -f "${CFG_DIR}/olduvai/play.json" ]; then
    echo "title_style_apply: FAIL — Apply did not persist play.json"
    cleanup; exit 1
fi

run_walk "down down enter esc wait wait shot quit" "${OUT2}"
if [ ! -s "${OUT2}/000.png" ]; then
    echo "title_style_apply: FAIL — no shot from the booted-enhanced walk"
    cleanup; exit 1
fi

if [ "$(sha256 "${OUT1}/000.png")" = "$(sha256 "${OUT2}/000.png")" ]; then
    echo "title_style_apply: PASS"
    cleanup; exit 0
fi

echo "title_style_apply: FAIL — title menu after Style Apply differs from a"
echo "  session booted enhanced (stale render state survived the reinit?)."
echo "  apply-at-title=${OUT1}/000.png  booted-enhanced=${OUT2}/000.png"
rm -rf "${CFG_DIR}"   # keep the shot dirs for eyeballing
exit 1
