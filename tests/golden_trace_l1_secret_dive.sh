#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 secret dive — the underwater room, entered by play.
#
# PROVENANCE — AN ADOPTED ORACLE SCENARIO, and that is what makes it different
# from the §3.15 gates around it.  The input is the reference repo's own
# `scenarios/l1_secret_dive.jsonl`, byte for byte.  It was ALREADY
# parity-verified before becoming a gate here: `olduvai_scenario_diff.py --all`
# replays it through both engines and reports frame-identical on 17 fields
# (26/26 corpus green, 2026-09-06).  So this fixture is cross-engine truth from
# the moment it lands — it did not have to be promoted afterwards, which is the
# whole argument for adopting the oracle's scenarios rather than recording new
# ones (BACKLOG §3.15 item 2).
#
# Reaches the secret room through normal play rather than `--start-screen`,
# so it covers the ENTRY path that golden_trace_secret skips.
#
# LABELLED `slow` — 44s.  The engine paces to 18.2 Hz even headless, so frame
# count IS wall-clock: 560 frames cannot run faster than it does.  The ten
# adopted scenarios are ~496 s together, which is why they are all behind the
# label and the everyday suite is unchanged.  `ctest --preset release-full`
# and `scripts/gate_local.sh` run them; `ctest --preset release` does not.
#
# Regenerate after an intentional change:
#   ./build/release/olduvai --play --level 1 \
#       --replay tests/fixtures/l1_secret_dive.jsonl \
#       --trace tests/fixtures/golden_l1_secret_dive.jsonl \
#       --play-frames 20000 --game-dir <game_dir>
# ...then RE-RUN THE ORACLE DIFF, or the parity claim above goes stale silently.
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_l1_secret_dive 1 - \
    l1_secret_dive.jsonl golden_l1_secret_dive.jsonl 560 132 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
