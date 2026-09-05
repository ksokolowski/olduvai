#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 fight trace gate — the first native gate whose input PRESSES ATTACK.
#
# WHY THIS EXISTS.  §3.15 measured the scenario corpus and found no fixture
# used the attack key at all — the club, the game's core verb, the only way a
# monster enters RunningAway.  monster_ai.cpp sat at 28.7% and collisions.cpp
# at 31.8% from the scenario side, and the RunningAway state machine (which was
# refactored on 2026-08-02) had 13 incidental executions across the whole
# suite.
#
# The fix was not to record: the ORACLE repo already had fight scenarios, they
# use the identical replay schema, and they replay deterministically on this
# engine unmodified.  This input is the reference's l1_walk_fight.jsonl,
# adopted verbatim (hand-authored key presses; no game content).
#
# WHAT KIND OF GOLDEN THIS IS.  The INPUT comes from the oracle side, but this
# TRACE was recorded from this engine — it pins behaviour change, not parity.
# The same input diffed through both engines via olduvai_scenario_diff.py is
# what gives parity, and that is tracked in §3.15 / RECORDING.md.
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_fight 1 - \
    fight_l1_in.jsonl golden_trace_l1_fight.jsonl 20000 400 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
