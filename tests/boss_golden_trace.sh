#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Boss-arena per-frame trace gate — the boss-side counterpart of golden_trace.
#
# WHY THIS EXISTS.  golden_trace pins 300 frames of L1 (the surface loop) and is
# the zero-tolerance contract that makes surface refactors provable.  The boss
# loop (run_boss_level, ~1900 lines) had NO equivalent: boss_replay_record only
# byte-compares a RECORDED INPUT FILE, which proves the recorder works, not that
# the fight behaves.  So every boss-side dedup/extraction in DEDUP_BACKLOG.md and
# SOC_ROADMAP.md was blocked on a gate that did not exist — and the pause work of
# 2026-07-24/25 found three real boss defects that no automated check could have
# caught.  This is the missing precondition, and it is additive: no production
# code changes, so it cannot regress the release it lands in.
#
# WHAT IT COVERS, HONESTLY.  The trace emitter records PLAYER-side state (pos,
# velocity, energy, lives, score, timer, frame_counter, club/axe flags,
# sprite_queue_count) — there are no boss-specific fields (boss pose, boss
# health) in the schema today.  So this pins the boss loop's physics, tick
# ordering, timing and RNG consumption as observed through the player; it does
# NOT pin boss AI internals.  Adding boss fields to the emitter would strengthen
# it and is a natural follow-up.
#
# Determinism: no input is fed, the fight is driven by the seeded integer LCG,
# and the run is capped at 300 frames — verified byte-identical across runs.
#
# Regenerate after an INTENTIONAL behaviour change (and say why in the commit):
#   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   ./build/release/olduvai --play --level 2 --play-frames 300 \
#       --trace tests/fixtures/golden_trace_boss_l2_300.jsonl --game-dir <dir>
#
# Skips (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/golden_trace_boss_l2_300.jsonl"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_golden_trace: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_golden_trace: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
if [ ! -f "${GOLDEN}" ]; then
    echo "boss_golden_trace: FAIL — golden fixture missing: ${GOLDEN}"
    exit 1
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
ACTUAL="$(mktemp /tmp/boss_golden_trace.XXXXXX)"

XDG_CONFIG_HOME="${CFG_DIR}" timeout 180 "${BINARY}" --play --level 2 \
    --play-frames 300 --trace "${ACTUAL}" --game-dir "${GAME_DIR}" \
    >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${ACTUAL}" ]; then
    echo "boss_golden_trace: FAIL — no trace produced (boss arena not reached?)"
    rm -f "${ACTUAL}"
    exit 1
fi

if diff -u --strip-trailing-cr "${GOLDEN}" "${ACTUAL}" \
        > /tmp/boss_golden_trace_diff.txt 2>&1; then
    echo "boss_golden_trace: OK — 300/300 boss frames match the fixture"
    rm -f "${ACTUAL}"
    exit 0
fi

echo "boss_golden_trace: FAIL — the boss trace diverged from the fixture."
echo "  golden=${GOLDEN}  actual=${ACTUAL}"
echo "  full diff: /tmp/boss_golden_trace_diff.txt"
echo ""
echo "First divergent lines:"
head -12 /tmp/boss_golden_trace_diff.txt
echo ""
echo "A divergence here means the boss loop's physics, tick ordering or RNG"
echo "consumption changed.  If that was intentional, regenerate the fixture in"
echo "the SAME commit and justify it in the message (see header)."
exit 1
