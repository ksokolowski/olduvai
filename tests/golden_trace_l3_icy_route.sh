#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Display-L3 surface route with attacks.
#
# PROVENANCE — AN ADOPTED ORACLE SCENARIO, and that is what makes it different
# from the §3.15 gates around it.  The input is the reference repo's own
# `scenarios/l3_icy_route.jsonl`, byte for byte.  It was ALREADY
# parity-verified before becoming a gate here: `olduvai_scenario_diff.py --all`
# replays it through both engines and reports frame-identical on 17 fields
# (26/26 corpus green, 2026-09-06).  So this fixture is cross-engine truth from
# the moment it lands — it did not have to be promoted afterwards, which is the
# whole argument for adopting the oracle's scenarios rather than recording new
# ones (BACKLOG §3.15 item 2).
#
# Display 3 = internal L5 (CLAUDE.md's swap).  The corpus otherwise reaches
# this level only by holding right and up.
#
# LABELLED `slow` — 19s.  The engine paces to 18.2 Hz even headless, so frame
# count IS wall-clock: 270 frames cannot run faster than it does.  The ten
# adopted scenarios are ~496 s together, which is why they are all behind the
# label and the everyday suite is unchanged.  `ctest --preset release-full`
# and `scripts/gate_local.sh` run them; `ctest --preset release` does not.
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 3 \
#       --replay tests/fixtures/l3_icy_route.jsonl \
#       --trace tests/fixtures/golden_l3_icy_route.jsonl \
#       --play-frames 20000 --game-dir <game_dir>
# ...then RE-RUN THE ORACLE DIFF, or the parity claim above goes stale silently.
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l3_icy_route 3 - \
    l3_icy_route.jsonl golden_l3_icy_route.jsonl 270 120 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
