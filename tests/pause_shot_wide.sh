#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless WIDESCREEN + HD Pause-overlay compose regression gate — CTest.
#
# The sibling pause_shot.sh pins the CLASSIC pause overlay (--render-scale 1,
# no --aspect widescreen, so compute_margin() returns 0 and the wide path never
# runs).  Nothing gated the HD/widescreen pause frame, which is exactly where
# the pause overlay dropped the widescreen margins and the HUD (the pause path
# re-composes a native 320 buffer and hands it to FramePresenter's pillarbox
# branch instead of the wide present).  This gate pins that frame.
#
# Golden = hash, not image: the content policy (CONTRIBUTING.md) forbids game
# imagery in the tree, and the shot contains decoded game artwork. On failure
# the shot PNG is kept in /tmp for eyeballing.
#
# Determinism (host-independent by construction):
#  - --hd-profile mmpx: INTEGER upscaler. NEVER switch this to omniscale —
#    its float codegen is not bit-stable across LTO relinks (see
#    wide_transition.sh, which pins goldens the same way).
#  - --window 896x400 + OLDUVAI_WS_FORCE_MARGIN=64 pin the margin and the
#    renderer output size on any host/driver (native_w 448 x scale 2).
#  - OLDUVAI_PAUSE_SHOT force-opens the overlay on frame 1, dumps, quits —
#    so no gameplay/RNG runs before the capture.
#  - XDG_CONFIG_HOME → fresh temp dir (the user's play.json cannot leak
#    enhanced/hd settings into the compose).
#  - Audio muted (dummy driver).
#
# Regenerate after an intentional change (then update the .sha256):
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_WS_FORCE_MARGIN=64 OLDUVAI_PAUSE_SHOT=/tmp/psw.png \
#   ./build/release/olduvai --play --level 1 --render-scale 2 \
#       --window 896x400 --enhanced --hd-profile mmpx --aspect widescreen \
#       --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/pause_wide_golden.sha256"
SHOT="$(mktemp -u /tmp/pause_shot_wide.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "pause_shot_wide: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "pause_shot_wide: SKIP — binary not found: ${BINARY}"
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
    OLDUVAI_PAUSE_SHOT="${SHOT}" timeout 60 \
    "${BINARY}" --play --level 1 --render-scale 2 --window 896x400 \
    --enhanced --hd-profile mmpx --aspect widescreen \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "pause_shot_wide: FAIL — no shot produced (pause overlay not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "pause_shot_wide: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "pause_shot_wide: FAIL — rendered HD/widescreen pause overlay differs from"
echo "  the golden hash.  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot: the margins must carry real neighbour terrain (not"
echo "  black bars) and the HUD must be drawn.  If the change is intentional,"
echo "  regenerate the hash (see header)."
exit 1
