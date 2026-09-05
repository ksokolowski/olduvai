#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Display-level 3 walk-and-jump — the corpus scenario that reaches
# check_l5_transition.
#
# WHY THIS EXISTS.  §3.15 left check_l5_transition and check_l7_transition among
# the functions nothing in the tree executed.  They are the per-level screen
# transition dispatchers (transitions.cpp check_screen_transition switches on
# current_level), and the corpus only ever ran L1 and the L2 arena.
#
# MIND THE SWAP: `state.current_level` is the INTERNAL id and display 3 <-> 5
# are exchanged (see CLAUDE.md).  Display level 3 is internal 5, which is why
# this file's name and its subject disagree.  Verified by coverage, not assumed.
#
# IT HAS TO ACTUALLY CROSS A SCREEN.  The first attempt used walk_in.jsonl —
# hold right — and the player died on screen 0 both times, game over, ZERO
# screen changes.  The transition function still showed 30-38% covered, because
# it is CALLED every frame and its clamps run; the transition logic itself never
# executed.  A gate written on that would have claimed a crossing it never made.
# Holding UP as well (walk_jump_in.jsonl, shared with golden_trace_climb) jumps
# the obstacles: 2 crossings, 0 -> 1 -> 2, and coverage rises
# to 50.0%.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine: it pins behaviour
# CHANGE, not correctness, like every scenario added under §3.15.
#
# Determinism: verified over two consecutive runs, byte-identical.
#
# Regenerate after an intentional change — and re-read the paragraph above:
#   ./build/release/olduvai --play --level 3 \\
#       --replay tests/fixtures/walk_jump_in.jsonl \\
#       --trace tests/fixtures/golden_trace_walk_l3.jsonl \\
#       --play-frames 300 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_walk_l3 3 - \
    walk_jump_in.jsonl golden_trace_walk_l3.jsonl 300 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
