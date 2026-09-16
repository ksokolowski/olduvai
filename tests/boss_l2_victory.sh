#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L2 T-Rex victory flash — the mid-flash wide frame, pinned by hash.
#
# WHY THIS EXISTS.  §3.7's boss-side dispatch slice folded five per-boss
# `internal_level ==` chains into a BossOps callback table and deliberately
# LEFT OUT the sixth — the victory-sprite chain — because its L2 and L6 arms
# were ungated: `boss_l4_victory` was the only victory gate in the tree.  Gate
# first is the rule that has held all session (§3.7 slice 2 built `level_fade`
# from the PRE-extraction binary so the gate proved the move rather than
# agreeing with itself).  This is the L2 half of that gate.
#
# THE CAPTURE HOOK ALREADY EXISTED AND NOTHING USED IT.  boss_app's victory
# block has carried an `OLDUVAI_REAL_SHOT && wsb.active && vf == 8` branch —
# render the mid-flash frame with do_present=false, then read the renderer
# back — since the L4 victory work.  It was reachable and deterministic the
# whole time; no test ever called it.  Building this gate needed no engine
# change at all, which is worth knowing before someone budgets for one.
#
# REACHING IT.  OLDUVAI_FORCE_WIN=<frame> sets `won` directly, and that IS
# enough here — unlike L4, whose ride-off is driven by win_flag phases INSIDE
# the fight loop and needed its own OLDUVAI_FORCE_L4_RIDEOFF.  L2's victory
# sequence runs AFTER the loop on `won`, so forcing the win is sufficient.
# The capture is at a fixed vf, so --play-shot-frame does not select it: 60,
# 70, 80 and 90 all yield the identical hash.  That is by design, not a bug —
# the gate wants one reproducible frame, not a frame index.
#
# WHAT IT PINS.  The flash frame composed WIDE: the clean arena mirrored with
# the 0.10 edge gradient and the victory sprites drawn once at origin_x =
# wsb.M, so the defeated T-Rex stays in the centre 320 rather than being
# reflected into the margins.  A regression to the old "mirror the baked 320
# frame" shape changes this hash.
#
# Determinism: verified over three consecutive runs, byte-identical.
# OLDUVAI_WS_FORCE_MARGIN pins the margin so window-size drift cannot move it.
#
# Regenerate after an intentional change:
#   OLDUVAI_FORCE_WIN=40 OLDUVAI_REAL_SHOT=1 OLDUVAI_WS_FORCE_MARGIN=48 \
#   SDL_VIDEODRIVER=dummy ./build/release/olduvai --play --level 2 \
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
GOLDEN="$(dirname "$0")/fixtures/boss_l2_victory.sha256"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_l2_victory: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_l2_victory: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum
    else shasum -a 256; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
SHOT="$(mktemp /tmp/boss_l2_victory.XXXXXX).png"

XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_FORCE_WIN=40 OLDUVAI_REAL_SHOT=1 \
    OLDUVAI_WS_FORCE_MARGIN=48 timeout 180 "${BINARY}" --play --level 2 \
    --game-dir "${GAME_DIR}" --enhanced --aspect widescreen \
    --window 896x400 \
    --play-shot "${SHOT}" --play-shot-frame 60 >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "boss_l2_victory: FAIL — no shot produced; the victory flash was not reached."
    echo "  Check OLDUVAI_FORCE_WIN still sets won, and that the vf == 8 capture"
    echo "  branch in boss_app's L2 victory block still exists."
    rm -f "${SHOT}"
    exit 1
fi
GOT="$(sha256 < "${SHOT}")"
if [ ! -f "${GOLDEN}" ]; then
    echo "boss_l2_victory: FAIL — no golden at ${GOLDEN} (got ${GOT})"
    rm -f "${SHOT}"
    exit 1
fi
if [ "${GOT}" = "$(cat "${GOLDEN}")" ]; then
    echo "boss_l2_victory: OK — victory flash frame matches"
    rm -f "${SHOT}"
    exit 0
fi
echo "boss_l2_victory: FAIL — hash mismatch"
echo "  expected $(cat "${GOLDEN}")"
echo "  got      ${GOT}"
echo "  shot kept at ${SHOT}"
exit 1
