#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 cave-with-a-BAT trace gate — the last function nothing executed.
#
# WHY THIS EXISTS.  §3.15's never-executed list ended with update_cave_bat, and
# the note against it said "needs a cave that HAS a bat".  golden_trace_cavebat
# enters the caves off screen 2, and those hold a SPIDER: update_cave_spider
# 100%, update_cave_bat 0%.  Sweeping the six L1 cave entrances under the
# instrumented binary answered which one: screen 6.
#
# Screen 6, hold right+down (the same cave_l1_in.jsonl golden_trace_cavebat uses):
# drop into cave 109, where update_cave_bat runs at 100%.
#
# Worth keeping the pair in mind: two caves on the same level, one with a
# spider and one with a bat, and only entering BOTH covers both handlers.  The
# entity a cave contains is not visible from the entrance.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine — behaviour CHANGE,
# not correctness, like every scenario added under §3.15.
#
# Determinism: verified over two consecutive runs, byte-identical.
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 1 --start-screen 6 \\
#       --replay tests/fixtures/cave_l1_in.jsonl \\
#       --trace tests/fixtures/golden_trace_l1_cavebat.jsonl \\
#       --play-frames 300 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_cavebat 1 6 \
    cave_l1_in.jsonl golden_trace_l1_cavebat.jsonl 300 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
