#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Display level 5, screen 18 — the LEVEL EXIT: hold right until the player
# walks off the right edge (check_l3_transition, no food gate) and the level
# completes on frame 35.
#
# WHY THIS EXISTS.  No other scenario ends in a level completion — the L1
# full clear ends in a game over — so the 8b intercept's last trace line was
# unpinned.  §3.29 moved the platform fade and tally out of the level loop
# (8b now breaks, as the reference does), which removed one line: the native
# engine used to write the completing frame, post-tally (screen 19, lives
# already +1), a line the reference never writes.
#
# ✅ CHECKED AGAINST THE REFERENCE 2026-09-17: this fixture's 35 lines
# (frames 0-34) equal the reference's frames 1-35 on all 17 compared fields
# (the usual one-frame offset; frame / frame_counter / sprite_queue_count
# ignored); the reference's next line is already the next level.  Compared
# by hand, not with olduvai_scenario_diff.py: the run continues into the next
# level on both engines, and that tool then aligns on the wrong frames.
#
# The tally runs for real after the loop (~25 s under the dummy driver).
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 5 --start-screen 18 \\
#       --replay tests/fixtures/l5s18_level_exit.jsonl \\
#       --trace tests/fixtures/golden_trace_l5s18_exit.jsonl \\
#       --play-frames 100 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l5s18_exit 5 18 \
    l5s18_level_exit.jsonl golden_trace_l5s18_exit.jsonl 100 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
