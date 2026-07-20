#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless main-menu compose regression gate — invoked by CTest.
#
# Renders ONE deterministic main-menu frame via the OLDUVAI_MAINMENU_SHOT hook
# under SDL's dummy video driver (software renderer) and compares its SHA-256
# to the committed golden hash. This gates the TitleMenuFlow (CC1) extraction,
# which golden_trace does NOT cover: olduvai_trace hard-starts in-level and
# never enters run_game's intro/attract/menu block.
#
# Golden = hash, not image: the content policy (CONTRIBUTING.md) forbids game
# imagery in the tree, and the shot contains decoded game artwork. On failure
# the shot PNG is kept in /tmp for eyeballing.
#
# Determinism (host-independent by construction):
#  - --render-scale 1 forces the INTEGER upscale path (the default x4 goes
#    through the float omniscale upscaler, whose codegen LTO reorders between
#    builds — a byte-exact golden there is non-reproducible).
#  - --window 640x400 pins the output size; without it the window is sized by
#    desktop_integer_scale() (desktop dims / 320x200), which varies per host
#    and video driver (SDL dummy desktop 1024x768 → x3, not the golden's x2).
#  - XDG_CONFIG_HOME points at an empty temp dir, so the user's play.json
#    (enhanced/hd keys!) can't leak into the compose — the golden is the
#    CLASSIC bitmap menu, pure engine defaults + these flags.
#
# Regenerate after an intentional menu change (then update the .sha256):
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_MAINMENU_SHOT=/tmp/mm.png ./build/release/olduvai --play \
#       --render-scale 1 --window 640x400 --game-dir <game_dir>
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data or the binary is absent.

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/mainmenu_golden.sha256"
SHOT="$(mktemp -u /tmp/mainmenu_shot.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "mainmenu_shot: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "mainmenu_shot: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

# sha256 <file> — portable (Linux sha256sum / macOS shasum).
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

# Software renderer, no window. --play with no --level enters the attract/menu
# (opts.frames must stay 0, so NO --play-frames — the shot hook self-quits after
# dumping the first menu frame; `timeout` is only a hang safety net).
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"   # mute test runs
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_MAINMENU_SHOT="${SHOT}" timeout 60 \
    "${BINARY}" --play --render-scale 1 --window 640x400 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "mainmenu_shot: FAIL — no shot produced (menu not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "mainmenu_shot: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "mainmenu_shot: FAIL — rendered menu differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot; if the menu renders correctly and the change is"
echo "  intentional, regenerate the hash (see header)."
exit 1
