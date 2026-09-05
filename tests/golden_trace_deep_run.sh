#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 deep-run trace gate — 161 seconds of real fighting play, adopted from
# the oracle repo's l1_deep_run.jsonl (see golden_trace_fight.sh for the full
# rationale; this is the long companion).
#
# Measured value: this single scenario takes monster_ai.cpp from 28.7% to
# 44.4% and collisions.cpp from 31.8% to 58.0% — it nearly doubles the fight
# coverage of the entire rest of the corpus.  ~79 s wall clock, which stays
# under boss_pause_shot's 90 s, so the suite's critical path is unchanged.
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_deep_run 1 - \
    deep_run_l1_in.jsonl golden_trace_l1_deep_run.jsonl 20000 400 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
