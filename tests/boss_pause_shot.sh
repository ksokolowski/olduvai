#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless BOSS-arena Pause-overlay compose regression gate — CTest.
#
# The boss pause overlay is a bespoke present block in boss_app.cpp that does
# NOT go through the boss present helpers, so nothing gated it: it skipped the
# HD upscale and the widescreen compose entirely and let SDL stretch a native
# 320x200 texture (nearest), i.e. the paused arena rendered as classic while
# the fight itself was HD + widescreen.  This gate pins that frame.
#
# The OLDUVAI_BOSS_PAUSE_SHOT hook force-opens the boss pause menu at frame 60
# (after the fly-in) and dumps one composed frame.
# OLDUVAI_BOSS_PAUSE_SCREEN picks the menu screen (default "pause_boss").
#
# Golden = hash, not image: the content policy (CONTRIBUTING.md) forbids game
# imagery in the tree.  On failure the shot PNG is kept in /tmp for eyeballing.
#
# Determinism: same recipe as pause_shot_wide.sh / wide_transition.sh —
# --hd-profile mmpx (INTEGER upscaler; never omniscale, its float codegen is
# not bit-stable across LTO relinks), --window 896x400 +
# OLDUVAI_WS_FORCE_MARGIN=64 (native_w 448 x scale 2), fresh XDG_CONFIG_HOME,
# muted audio.  The capture is a fixed frame number, so no input timing enters.
#
# Regenerate after an intentional change (then update the .sha256):
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_WS_FORCE_MARGIN=64 OLDUVAI_BOSS_PAUSE_SHOT=/tmp/bps.png \
#   ./build/release/olduvai --play --level 2 --render-scale 2 \
#       --window 896x400 --enhanced --hd-profile mmpx --aspect widescreen \
#       --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/boss_pause_golden.sha256"
SHOT="$(mktemp -u /tmp/boss_pause_shot.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_pause_shot: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_pause_shot: SKIP — binary not found: ${BINARY}"
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
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_WS_FORCE_MARGIN=64 \
    OLDUVAI_BOSS_PAUSE_SHOT="${SHOT}" timeout 90 \
    "${BINARY}" --play --level 2 --render-scale 2 --window 896x400 \
    --enhanced --hd-profile mmpx --aspect widescreen \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "boss_pause_shot: FAIL — no shot produced (boss pause not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "boss_pause_shot: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "boss_pause_shot: FAIL — rendered boss pause overlay differs from the"
echo "  golden hash.  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot: the arena must be HD-filtered (not a nearest-stretched"
echo "  320 frame) and the margins must be the mirrored arena edge.  If the"
echo "  change is intentional, regenerate the hash (see header)."
exit 1
