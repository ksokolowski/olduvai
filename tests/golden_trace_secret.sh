#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 secret-room trace gate — the corpus scenario that goes UNDERWATER.
#
# WHY THIS EXISTS.  §3.15 measured secret.cpp at 17.9% with three functions
# nothing in the tree executed: check_secret_exit, update_secret_trampoline and
# setup_secret_collision.  The corpus walked L1 left-to-right and fought a boss;
# it never fell through a floor trap.
#
# Screen 5, hold left: the player crosses x<105 with y>155 and drops into
# secret 0, then cycles in and out four times in 170 frames.  That takes
# secret.cpp from 17.9% to 77.4% of lines, with ALL SEVEN of its functions
# executed.
#
# WHAT KIND OF GOLDEN THIS IS.  The same kind as golden_trace_walk, and the
# same caveat: recorded from THIS engine, so it pins behaviour CHANGE, not
# correctness.  golden_trace and boss_golden_trace were validated frame by
# frame against the Python oracle; this one has not been.  Promoting it means
# running secret_l1_in.jsonl through the reference and diffing — BACKLOG §3.15.
#
# Determinism: verified over two consecutive runs, byte-identical.  The run
# ends 18 frames after the script's last event (frame_input.cpp:23).
#
# Regenerate after an intentional change — and re-read the paragraph above:
#   ./build/release/olduvai --play --level 1 --start-screen 5 \
#       --replay tests/fixtures/secret_l1_in.jsonl \
#       --trace tests/fixtures/golden_trace_l1_secret.jsonl \
#       --play-frames 300 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_secret 1 5 \
    secret_l1_in.jsonl golden_trace_l1_secret.jsonl 300 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
