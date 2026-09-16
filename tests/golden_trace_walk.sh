#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# L1 walk-across trace gate — the corpus scenario that LEAVES screen 0.
#
# WHY THIS EXISTS.  §3.15 measured what the trace corpus actually executes and
# found 15.3% of src/systems.  The cause was not subtlety: golden_trace runs L1
# screen 0 and boss_golden_trace runs the L2 arena, so nothing in the corpus
# ever crossed a screen boundary.  transitions.cpp sat at 0.00%.
#
# This walks right across L1 for 300 frames.  In doing so it crosses three
# screen boundaries (frames 37, 93, 149), takes contact damage, and loses two
# lives to death-and-respawn.  Adding it took the corpus from 15.3% to 34.2% of
# systems lines and 23.5% to 44.8% of its functions — spawning 11->74%,
# player 35->78%, frame_runner 32->56%, transitions 0->14%.
#
# WHAT KIND OF GOLDEN THIS IS.  ✅ ORACLE-VERIFIED 2026-09-06 (§3.15 item 1).
# It was originally recorded from THIS engine and pinned behaviour CHANGE only;
# it has since been promoted.  walk_in.jsonl was replayed through both engines
# and the traces diffed with frame-offset alignment
# (olduvai_scenario_diff.py): 300 aligned frames IDENTICAL on 17 fields.  The
# input now lives in the reference corpus as scenarios/l1_walk_hold_right.jsonl,
# so `--all` keeps it verified rather than leaving this a one-off measurement.
#
# So this fixture is cross-engine truth, like golden_trace and
# boss_golden_trace — it may be cited as parity.  What that does NOT mean: the
# diff aligns on gameplay state and compares 17 trace fields, so anything
# outside those fields (rendering, audio, sub-frame pacing) is still unproven
# here.  Re-run the diff after regenerating, or the promotion goes stale
# silently.
#
# Determinism: verified over three consecutive runs, byte-identical.  The LCG
# is seeded by level entry, the dummy drivers mute audio and video, and the
# replay script pins every input; the run ends 18 frames after the script's
# last event (frame_input.cpp:23), which is what fixes the length at 300.
#
# Regenerate after an intentional change — and re-read the paragraph above
# before you do:
#   ./build/release/olduvai --play --level 1 --replay tests/fixtures/walk_in.jsonl \
#       --trace tests/fixtures/golden_trace_l1_walk.jsonl --play-frames 400 \
#       --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

exec sh "$(dirname "$0")/lib/trace_gate.sh" golden_trace_walk 1 - \
    walk_in.jsonl golden_trace_l1_walk.jsonl 400 180 \
    "${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}" \
    "${2:-$(dirname "$0")/../build/release/olduvai}"
