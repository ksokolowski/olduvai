#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Display level 5 (internal L3), screen 9: climb the vine, drop onto the
# ledge in front of the tree trunk and press DOWN in its doorway — the
# [0x987e] warp to screen 10.
#
# WHY THIS EXISTS.  No scenario crossed a [0x987e] warp (the L3 trunk, the
# L7 lava-spring door), and both engines had it wrong the same way: they
# warped in the trigger frame, where the EXE plays Player_UpdateAndDraw's
# descent first and warps only once the low bits read 3 (FUN_27f7_1b51
# +0x1b63..0x1b9c; FUN_2276_06f2 +0x0e02..0x0e10).  Frames 122-125 pin it:
# freeze 4001 (trigger), 4002, 4003, then screen 10 at (100, 159).  The first
# cross-engine run also caught the reference keeping the 1000 across the
# screen change, where the EXE zeroes it (+0x0958); fixed there.
#
# ✅ CHECKED AGAINST THE REFERENCE 2026-09-27: `olduvai_scenario_diff.py
# scenarios/l5s9_trunk_warp.jsonl` — 154 aligned frames identical on 17
# fields.
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 5 --start-screen 9 \\
#       --replay tests/fixtures/l5s9_trunk_warp.jsonl \\
#       --trace tests/fixtures/golden_trace_l5s9_trunk_warp.jsonl \\
#       --play-frames 160 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l5s9_trunk_warp 5 9 \
    l5s9_trunk_warp.jsonl golden_trace_l5s9_trunk_warp.jsonl 160 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
