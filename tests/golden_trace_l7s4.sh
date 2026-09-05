#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Display level 7, screen 4 — a CLAMP scenario for check_l7_transition.
#
# WHY THIS EXISTS.  §3.16 wants the ~15 per-screen walk clamps
# (`if (scr == N && p.x < K) p.x = K`) turned into a table.  Before touching
# them, coverage was asked which of those lines any gate actually executes.
# NINE OF FIFTEEN: ZERO.  check_l3_transition is internal L3 = DISPLAY 5, and
# no scenario ran display 5 at all; L7's screen-6 pair sat on a screen nothing
# reached.  Restructuring them would have been a refactor under a gate that was
# not looking.
#
# These scenarios are not traversals and are not meant to be.  The screens they
# sit on are exactly the ones whose clamps HOLD THE PLAYER IN PLACE — display 5
# screens 10 and 11 are the balloon-flight screens, x pinned to [100, 0xCC] —
# so a run here crosses no boundary by design, and 0 screen changes is the
# clamp working rather than the scenario failing.
#
# Between them: display 5 screen 10 covers the screen-10 branch, screen 11 the
# screen-11 branch, and display 7 screen 4 reaches screens 4-6.  All fifteen
# clamp lines then execute.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine — behaviour CHANGE,
# not correctness, like every scenario added under §3.15.
#
# Determinism: verified over two consecutive runs, byte-identical.
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 7 --start-screen 4 \\
#       --replay tests/fixtures/walk_jump_in.jsonl \\
#       --trace tests/fixtures/golden_trace_l7s4.jsonl \\
#       --play-frames 300 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l7s4 7 4 \
    walk_jump_in.jsonl golden_trace_l7s4.jsonl 300 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
