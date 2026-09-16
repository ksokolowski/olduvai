#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 cave trace gate — the corpus scenario that goes UNDERGROUND.
#
# ✅ ORACLE-VERIFIED 2026-09-06 (§3.15 item 1, second pass) — this fixture is
# cross-engine truth and may be cited as parity.  It was recorded from THIS
# engine and pinned behaviour CHANGE only; it could not be promoted at first
# because the diff harness had no way to enter a level mid-way, so every gate
# starting at a non-zero screen was excluded.  The reference gained
# `--start-screen` (mirroring this engine's flag field for field) and the diff
# tool learned the lNsM_ filename convention, so `l1s2_cave.jsonl`
# now runs `--level 1 --start-screen 2` on BOTH engines.
#
# 170 aligned frames IDENTICAL on 17 fields — the exact length of this gate's
# own fixture, so the promotion covers the whole run and not a prefix.  Re-run
# the diff after regenerating, or the promotion goes stale in silence.
#
# WHY THIS EXISTS.  §3.15 measured cave_logic.cpp at 3.1%, with
# setup_cave_collision and update_cave_bat executed by nothing in the tree.
# The reason was one key: cave entry requires holding DOWN on the entrance
# (collisions.cpp check_cave_entrance), and no corpus scenario ever pressed it.
# A sweep of L1 walking right found no caves at all; the same sweep with DOWN
# held found them on screens 0, 1, 2, 3, 6 and 13.
#
# Screen 2, hold right+down: the player drops into cave 102 at frame 21 and
# reaches 103 as well.  That takes cave_logic.cpp from 3.1% to 70.4% of lines
# and 4 of its 5 functions.  The fifth is update_cave_bat, which needs a cave
# that HAS a bat — a follow-up scenario, not a defect.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine, so it pins behaviour
# CHANGE, not correctness — same caveat as golden_trace_walk and
# golden_trace_secret, and the same fix: run cave_l1_in.jsonl through the
# reference and diff (BACKLOG §3.15).
#
# Determinism: verified over two consecutive runs, byte-identical.
#
# Regenerate after an intentional change — and re-read the paragraph above:
#   ./build/release/olduvai --play --level 1 --start-screen 2 \\
#       --replay tests/fixtures/cave_l1_in.jsonl \\
#       --trace tests/fixtures/golden_trace_l1_cave.jsonl \\
#       --play-frames 300 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_cave 1 2 \
    cave_l1_in.jsonl golden_trace_l1_cave.jsonl 300 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
