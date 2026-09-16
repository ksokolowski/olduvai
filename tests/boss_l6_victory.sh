#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L6 Giant victory drop — the mid-drop wide frame, pinned by hash.
#
# WHY THIS EXISTS.  §3.7's boss-side dispatch slice folded five per-boss
# chains into a BossOps callback table and LEFT OUT the sixth — the
# victory-sprite chain — because its L2 and L6 arms were ungated.  L2 turned
# out to need no engine work (its hook already existed and nothing called it).
# L6 was the real gap: it was the ONLY boss ending in the tree that no test
# could photograph.
#
# THE HOOK HAD TO BE BUILT, unlike L2's.  L6's victory is a
# `while (l6.win_flag != 100)` drop loop with no frame counter, and
# present_l6_victory_wide() was a no-argument lambda that ALWAYS presented — so
# there was nothing to read back.  It now takes do_present, and the loop counts
# victory frames so a fixed index can be captured.
#
# WHERE THE CAPTURE SITS, AND WHY THERE.  Before the smooth sub-frame branch,
# not inside it: boss_use_float is still false at that point, so the frame is
# rendered at INTEGER positions and does not depend on which sub-frame the
# drop lerp happens to be in.  Capturing inside the lerp would have pinned a
# hash to sub-frame timing, which is exactly the kind of golden that fails for
# reasons unrelated to a regression.
#
# REACHING IT.  OLDUVAI_FORCE_WIN=<frame> sets `won`, and L6's victory runs
# AFTER the fight loop on it — the same reason L2 needs no bespoke force flag
# and L4 does (its ride-off is driven by win_flag phases INSIDE the loop).
#
# WHAT IT PINS.  The drop frame composed WIDE: the clean arena mirrored with
# the 0.10 edge gradient and the victory sprites drawn once at origin_x =
# wsb.M, so the beaten giant and the landing player stay in the centre 320
# instead of being reflected into the margins.  Verified distinct from a
# same-frame FIGHT capture, so the gate cannot silently pass on the wrong path.
#
# Determinism: verified over three consecutive runs, byte-identical.
#
# Regenerate after an intentional change:
#   OLDUVAI_FORCE_WIN=40 OLDUVAI_REAL_SHOT=1 OLDUVAI_WS_FORCE_MARGIN=48 \
#   SDL_VIDEODRIVER=dummy ./build/release/olduvai --play --level 6 \
#       --game-dir <dir> --enhanced --aspect widescreen --window 896x400 \
#       --play-shot <out> --play-shot-frame 60
#
# GOLDEN RE-BLESSED 2026-09-07 — the old hash pinned a DEFECT, not a frame.
# capture_renderer_output read the VIEWPORT (SDL_RenderReadPixels rect=nullptr)
# without clearing the logical size, so this shot came out shifted flush-left
# with the letterbox bar doubled on the right (content 0..831, bars 0/64).  The
# corrected capture is centred (content 32..863, bars 32/32).  Proven, not
# assumed: a build with the fix reverted reproduces the OLD hash exactly.
# See BACKLOG §3.14's recurrence note.
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/boss_l6_victory.sha256"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_l6_victory: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_l6_victory: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum
    else shasum -a 256; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
SHOT="$(mktemp /tmp/boss_l6_victory.XXXXXX).png"

XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_FORCE_WIN=40 OLDUVAI_REAL_SHOT=1 \
    OLDUVAI_WS_FORCE_MARGIN=48 timeout 180 "${BINARY}" --play --level 6 \
    --game-dir "${GAME_DIR}" --enhanced --aspect widescreen \
    --window 896x400 \
    --play-shot "${SHOT}" --play-shot-frame 60 >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "boss_l6_victory: FAIL — no shot produced; the victory drop was not reached."
    echo "  Check OLDUVAI_FORCE_WIN still sets won, and that the l6_vf == 8"
    echo "  capture branch in boss_app's L6 victory drop loop still exists."
    rm -f "${SHOT}"
    exit 1
fi
GOT="$(sha256 < "${SHOT}")"
if [ ! -f "${GOLDEN}" ]; then
    echo "boss_l6_victory: FAIL — no golden at ${GOLDEN} (got ${GOT})"
    rm -f "${SHOT}"
    exit 1
fi
if [ "${GOT}" = "$(cat "${GOLDEN}")" ]; then
    echo "boss_l6_victory: OK — victory drop frame matches"
    rm -f "${SHOT}"
    exit 0
fi
echo "boss_l6_victory: FAIL — hash mismatch"
echo "  expected $(cat "${GOLDEN}")"
echo "  got      ${GOT}"
echo "  shot kept at ${SHOT}"
exit 1
