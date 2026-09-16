#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L6 slam-kill trace under smooth motion — the gate for the §3.3c pose-hold.
#
# WHY THIS EXISTS.  The reference's L6 slam pose-hold (_advance_frame_counter,
# 2 logic ticks per pose in the attack window) was ported in 6c325d9 (2026-06-15)
# — and then sat UNGATED for ten weeks, because no automated run could reach it:
# smooth motion is forced off under --play-frames (max_frames guard) AND under
# --trace (the sub-frame-count determinism rule).  This gate is the escape:
#
#   OLDUVAI_FORCE_SMOOTH=1 keeps smooth on through both guards.  It is sound
#   here and only here: the blanket --trace rule exists because render
#   SUB-FRAME counts track the display refresh, while this trace pins
#   per-LOGIC-tick state, which DosTicker paces deterministically either way.
#   options_build.cpp carries the same warning — never run this combination
#   interactively.
#
# WHAT THE SCENARIO DOES.  Walk onto the punch plate (x in [100,130]) and stand
# there: the giant's slam kill zone covers the plate on frame_counter 17, so
# the player dies once per slam cycle until game over.  The pose-hold stretches
# the cycle (~25 -> ~30 ticks), which moves every kill: classic deaths at
# t=111/183/252/315, smooth at t=120/204/309/370 — 224 of 371 frames differ
# from the classic run of the same input.  A regression that drops the hold or
# changes its phase reverts those frames to the classic cadence and fails.
#
# Bonus coverage: this is the only scenario that exercises the L6 slam-kill
# routing (check_slam_damage -> boss_player_hit) and a boss-side game over.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine — the reference's
# scenario diff runs classic only, so parity for the hold rests on the port
# commit's line-by-line reading of boss_l6.py::_advance_frame_counter.  This
# fixture pins behaviour CHANGE.
#
# ⚠️ STILL TRUE AFTER §3.15 ITEM 1, AND THE NEAR-MISS IS WORTH RECORDING.
# l6_slam_deaths.jsonl WAS run through olduvai_scenario_diff.py on 2026-09-06
# and came back green — 315 aligned frames identical on 17 fields — and it
# would have been easy to write that up as "this gate is now oracle-verified".
# It is not.  The diff runs both engines in their DEFAULT mode; this gate runs
# the native side with OLDUVAI_FORCE_SMOOTH=1 --enhanced --render-scale 2.
# Measured rather than argued: the fixture here is 371 frames, the diff's
# native run is 316, and they part company at frame 92.  Two different runs.
#
# So what IS oracle-verified is l6_slam_deaths in CLASSIC mode — real new
# coverage of the slam-kill routing and the boss-side game over, and it lives
# in the reference corpus as scenarios/l6_slam_deaths.jsonl.  The smooth-motion
# pose-hold that this gate exists for is NOT covered by it, and cannot be until
# the diff tool can drive both sides in a matching mode.  The general rule the
# near-miss gives: a promotion only counts if the diffed invocation is the
# gate's invocation — check env vars and flags, not just the input script.
#
# Determinism: verified byte-identical over two consecutive runs (mmpx is the
# integer upscaler; the trace holds no pixels).
#
# Regenerate after an intentional change:
#   OLDUVAI_FORCE_SMOOTH=1 SDL_VIDEODRIVER=dummy ./build/release/olduvai \
#       --play --level 6 --replay tests/fixtures/l6_slam_deaths.jsonl \
#       --trace tests/fixtures/golden_trace_l6_slam_hd.jsonl \
#       --play-frames 600 --enhanced --hd-profile mmpx --render-scale 2 \
#       --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

TRACE_GATE_FLAGS="--enhanced --hd-profile mmpx --render-scale 2" \
    exec env OLDUVAI_FORCE_SMOOTH=1 sh "$(dirname "$0")/lib/trace_gate.sh" \
    golden_trace_l6_slam_hd 6 - \
    l6_slam_deaths.jsonl golden_trace_l6_slam_hd.jsonl 600 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
