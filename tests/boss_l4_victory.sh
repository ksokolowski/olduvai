#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L4 ride-off victory compose — the wide victory frame, pinned by hash.
#
# WHY THIS EXISTS.  §3.7 cluster 3 refactored the L4-victory branch of
# boss_app's present_any, and nothing in the tree could photograph it.  Two
# separate reasons, both real:
#
#   1. No way to REACH it.  OLDUVAI_FORCE_WIN sets `won` directly and leaves
#      l4.win_flag at 0, so the fight loop ends before the ride-off draws a
#      single frame.  The ride-off needs win_flag seeded at 1, from which
#      1 -> 2 -> 3 -> 100 runs on its own.  Hence OLDUVAI_FORCE_L4_RIDEOFF.
#
#   2. No way to CAPTURE it.  The OLDUVAI_REAL_SHOT branch tested for an L4
#      victory frame and then deliberately fell through to present_frame — the
#      PILLARBOX present.  So a victory screenshot showed a pillarboxed fight
#      frame, and the wide ride-off compose appeared in no image anywhere.  It
#      now routes through present_any, the branch that draws it.
#
# WHAT IT PINS.  The ride-off composes the arena background wide (mirror + the
# 0.10 edge gradient) and draws the victory sprites ONCE at origin_x = wsb.M so
# the triceratops and player OVERFLOW into the margins.  The path it replaced
# baked sprites into a 320 frame and mirrored THAT, which duplicated and clipped
# the dino at the screen edges.  A regression to the old shape changes this hash.
#
# Golden = the frame's SHA-256, never the image: decoded game artwork must not
# enter the tree (CONTRIBUTING.md).
#
# RE-BASELINED 2026-08-23, deliberately: the l4_boss_fight scenario caught the
# native spawning the L4 fighter at x=60 where FUN_24cc_02f2 writes 0xD2=210;
# fixing that moved where the player stands when OLDUVAI_FORCE_L4_RIDEOFF=40
# seeds the victory, so the compose at frame 55 legitimately changed.  The old
# golden photographed the ride-off of the DIVERGED spawn.  New golden verified
# deterministic over two runs and still wide-composed (896x400, sprite content
# in both margins — the overflow shape, not the bake-then-mirror one).
#
# Determinism: verified over two consecutive runs.  OLDUVAI_WS_FORCE_MARGIN pins
# the margin so the wide canvas does not vary with the host desktop.
#
# Regenerate after an intentional change:
#   OLDUVAI_FORCE_L4_RIDEOFF=40 OLDUVAI_REAL_SHOT=1 OLDUVAI_WS_FORCE_MARGIN=48 \
#   SDL_VIDEODRIVER=dummy ./build/release/olduvai --play --level 4 \
#       --game-dir <dir> --enhanced --hd-profile mmpx --render-scale 2 \
#       --aspect widescreen --window 896x400 --play-shot <out> --play-shot-frame 55
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/boss_l4_victory.sha256"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_l4_victory: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_l4_victory: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum
    else shasum -a 256; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
SHOT="$(mktemp /tmp/boss_l4_victory.XXXXXX).png"

XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_FORCE_L4_RIDEOFF=40 OLDUVAI_REAL_SHOT=1 \
    OLDUVAI_WS_FORCE_MARGIN=48 timeout 180 "${BINARY}" --play --level 4 \
    --game-dir "${GAME_DIR}" --enhanced --hd-profile mmpx --render-scale 2 \
    --aspect widescreen --window 896x400 \
    --play-shot "${SHOT}" --play-shot-frame 55 >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "boss_l4_victory: FAIL — no shot produced; the ride-off was not reached."
    echo "  Check OLDUVAI_FORCE_L4_RIDEOFF still seeds l4.win_flag."
    rm -f "${SHOT}"
    exit 1
fi
GOT="$(sha256 < "${SHOT}")"
if [ ! -f "${GOLDEN}" ]; then
    echo "boss_l4_victory: FAIL — no golden at ${GOLDEN} (got ${GOT})"
    rm -f "${SHOT}"
    exit 1
fi
if [ "${GOT}" = "$(cat "${GOLDEN}")" ]; then
    echo "boss_l4_victory: OK — ride-off frame matches"
    rm -f "${SHOT}"
    exit 0
fi
echo "boss_l4_victory: FAIL — the ride-off compose differs from the golden."
echo "  shot=${SHOT}  golden=${GOLDEN}   got ${GOT}"
echo "  Sprites duplicated or clipped at the screen edges means the wide"
echo "  compose regressed to the bake-then-mirror shape (see the header)."
exit 1
