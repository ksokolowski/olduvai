#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L6 giant boss-fight trace gate — the corpus scenario for the third fight.
#
# WHY THIS EXISTS.  Same gap as golden_trace_l4_fight: nothing replayed the
# L6 loop.  This fight is mechanically distinct from L2/L4 — the floor plate
# at x in (99,125) AUTO-LAUNCHES a grounded player (jump_peak 70), the only
# hitting window is airborne near the head (club_flag==1 && y<50 && x>182),
# and the slam is an instant kill on frame_counter 17/18 over the plate and
# low sweep zones.  It is also the fight whose smooth-motion pose-hold
# (§3.3c) is still unported, so any future port re-baselines against THIS.
#
# WHAT THE SCENARIO DOES (authored against systems/boss_l6.cpp, first
# iteration landed): five cycles of "walk to plate -> auto-launch -> drift
# right while high -> swing burst -> walk back".  Five damage transitions
# (317 -> 307) at t=67/73/379/385/514, zero deaths across 800 frames — the
# slam windows are dodged this time; the kill path itself is covered by the
# L4 scenario and golden_trace_l2.
#
# Determinism: run-to-run byte-identical over two runs.
#
# WHAT KIND OF GOLDEN THIS IS.  Recorded from THIS engine from oracle-shaped
# inputs: it pins behaviour CHANGE until promoted by olduvai_scenario_diff.py
# against the reference (RECORDING.md).
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 6 \
#       --replay tests/fixtures/l6_boss_fight.jsonl \
#       --trace tests/fixtures/golden_trace_l6_fight.jsonl \
#       --play-frames 800 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l6_fight 6 - \
    l6_boss_fight.jsonl golden_trace_l6_fight.jsonl 800 240 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
