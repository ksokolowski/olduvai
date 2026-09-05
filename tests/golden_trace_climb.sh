#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 climbing trace gate — the corpus scenario that goes UP A LADDER.
#
# WHY THIS EXISTS.  §3.15 left update_climbing among the functions nothing in
# the tree executed.  Climbing needs an ObjType::Stairs entity AND the UP key
# held at the right vertical alignment (collisions.cpp check_climb), and the
# corpus held UP nowhere: golden_trace_walk walks right, the cave scenario
# holds DOWN.
#
# The JSONL trace has no `climbing` field, so the detector for this was coverage
# itself — a sweep of twelve L1 screens under the instrumented binary, checking
# which ones executed update_climbing.  Screens 0, 3 and 5 do.  Screen 0 is
# used here because golden_trace_walk already starts there, so the two
# scenarios differ by exactly one key and the pair reads as a controlled
# comparison.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine — it pins behaviour
# CHANGE, not correctness, exactly like golden_trace_walk/_secret/_cave.
# Promoting it means running walk_jump_in.jsonl through the reference and
# diffing (BACKLOG §3.15).
#
# Determinism: verified over two consecutive runs, byte-identical.
#
# Regenerate after an intentional change — and re-read the paragraph above:
#   ./build/release/olduvai --play --level 1 --start-screen 0 \\
#       --replay tests/fixtures/walk_jump_in.jsonl \\
#       --trace tests/fixtures/golden_trace_l1_climb.jsonl \\
#       --play-frames 300 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_climb 1 0 \
    walk_jump_in.jsonl golden_trace_l1_climb.jsonl 300 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
