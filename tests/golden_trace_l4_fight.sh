#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L4 triceratops boss-fight trace gate — the corpus scenario that lands hits
# on the second boss.
#
# WHY THIS EXISTS.  §3.15/RECORDING.md: only L2 had a boss scenario, so the L4
# fight loop (patrol + stun snap, tail-side club-hit routing, instant-kill
# knockback zones, death/rise-skip/respawn/i-frame cycle) ran under no
# cross-engine or replay gate at all.  boss_golden_trace covers --level 2 only;
# boss_l4_victory seeds the ride-off with OLDUVAI_FORCE_L4_RIDEOFF and so never
# plays the fight itself.
#
# WHAT THE SCENARIO DOES (authored against systems/boss_l4.cpp, verified
# empirically — see BACKLOG §3.15): spawn is x=210 per FUN_24cc_02f2 (a divergence
# this scenario caught on its first run: native used the shared x=60 init);
# face left after the fly-in, anchor there, swing every 6 ticks.  The boss's
# rightward pass through the anchor is lethal and kills once per ~89-tick
# patrol; respawn i-frames (39 ticks) then cover the leftward leg's SAFE tail
# pocket, where the scheduled swings land.  Seven damage transitions
# (317 -> 303), four deaths at t=86/175/264/353, all three lives spent —
# patrol, snap+stun, hit routing, kill zones, the fire-skipped rise and the
# i-frame window are all on the trace.
#
# Determinism: run-to-run byte-identical over two runs; deaths repeat at
# frames 86/175/264/353 exactly.
#
# WHAT KIND OF GOLDEN THIS IS.  Oracle-promoted: olduvai_scenario_diff.py
# replays the same input through the reference engine and the traces agree
# frame-for-frame (see BACKLOG §3.15).  Until that run this fixture pinned
# behaviour CHANGE only.
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 4 \
#       --replay tests/fixtures/l4_boss_fight.jsonl \
#       --trace tests/fixtures/golden_trace_l4_fight.jsonl \
#       --play-frames 470 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l4_fight 4 - \
    l4_boss_fight.jsonl golden_trace_l4_fight.jsonl 470 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
