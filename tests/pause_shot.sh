#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless in-game Pause-overlay compose regression gate — invoked by CTest.
#
# Renders ONE deterministic Pause frame via the OLDUVAI_PAUSE_SHOT hook (force-
# opens the pause overlay on frame 1 of L1, dumps it, quits) under the dummy
# video driver, and compares its SHA-256 to the committed golden hash. Gates
# the pause-controller extraction (CC2c), which golden_trace does not cover.
#
# Golden = hash, not image: the content policy (CONTRIBUTING.md) forbids game
# imagery in the tree, and the shot contains decoded game artwork. On failure
# the shot PNG is kept in /tmp for eyeballing.
#
# Determinism (host-independent by construction):
#  - --render-scale 1 → INTEGER upscale path, classic bitmap overlay (see the
#    mainmenu_shot determinism note).
#  - --window 640x400 pins the output size (otherwise desktop_integer_scale()
#    sizes the window from the host desktop — varies per host/driver).
#  - XDG_CONFIG_HOME → empty temp dir, so the user's play.json can't leak
#    enhanced/hd settings into the compose.
#
# Regenerate after an intentional change (then update the .sha256):
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_PAUSE_SHOT=/tmp/ps.png ./build/release/olduvai --play --level 1 \
#       --render-scale 1 --window 640x400 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/pause_golden.sha256"
SHOT="$(mktemp -u /tmp/pause_shot.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "pause_shot: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "pause_shot: SKIP — binary not found: ${BINARY}"
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
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_PAUSE_SHOT="${SHOT}" timeout 60 \
    "${BINARY}" --play --level 1 --render-scale 1 --window 640x400 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "pause_shot: FAIL — no shot produced (pause overlay not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "pause_shot: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "pause_shot: FAIL — rendered pause overlay differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot; if the overlay renders correctly and the change is"
echo "  intentional, regenerate the hash (see header)."
exit 1
