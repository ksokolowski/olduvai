#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Measure a boss level's per-present cost on a REAL display / device.
#
# WHY.  SPIKE_BOSS_PERF.md closes with "re-measure rather than trust" — the
# boss-level numbers there are a before/after delta, and a delta needs the
# after-run to use the byte-identical invocation.  This is that invocation,
# fixed so the two runs cannot drift apart.
#
# The config is the shipped handheld one, stated in full so no play.json or
# launcher edit can move the numbers — every mode is pinned on the CLI:
#   SDL_VIDEODRIVER=mali, OLDUVAI_NO_VSYNC=1, OLDUVAI_SMOOTH_SUBFRAMES=2,
#   OLDUVAI_FORCE_SMOOTH=1 (a frame-capped run otherwise forces smooth OFF,
#   boss_app.cpp:555), OLDUVAI_FRAME_STATS=1 (so the driver prints its stats),
#   --enhanced --transitions smooth --profile hd --aspect widescreen
#   --render-scale 3 --hd-profile smooth.
#
# --level 2/4/6 drops straight into the boss arena (the bosses are the 2nd/4th/
# 6th sequence positions), so no walk needs replaying.
#
# A segment is a level (the fight, <frames> of it) or a level with a `v`
# (its victory: 2v, 4v, 6v).  A victory run forces the win at fight frame 20
# (OLDUVAI_FORCE_WIN; L4's ride-off needs OLDUVAI_FORCE_L4_RIDEOFF, which
# FORCE_WIN cannot reach), has no frame cap (a capped run skips the victory)
# and stops after the level (OLDUVAI_STOP_AFTER_LEVEL).  L2 and L6 print a
# separate "frame-stats L<n> victory:" line; L4's ride-off runs inside the
# fight loop, so it is the "frame-stats L4:" line, 20 fight frames and the
# rest ride-off.
#
# OLDUVAI_GAME_DATA overrides the game dir (on a dev machine), and
# BOSS_MEASURE_EXTRA adds flags (e.g. "--window 1024x600" for the A12's
# margin on a desktop, SDL_VIDEODRIVER=cocoa or x11 there).
#
# Stats go to STDERR (frame_stats.cpp report).  Capture it; the line shape is
#   frame-stats L%d: ... bg_copy=%.1fms scene=%.1fms ... compose=%.1fms
#                   | PACING eff_hz=%.2f ...
# and the SPIKE_BOSS_PERF table quotes exactly those columns.

USAGE="usage: $0 <binary> <frames> [segments...]   (default: 2 4 6; victories: 2v 4v 6v)"

BINARY="${1:?$USAGE}"
FRAMES="${2:?$USAGE}"
shift 2 2>/dev/null
SEGMENTS="${*:-2 4 6}"

if [ ! -x "${BINARY}" ]; then
    echo "boss_measure: binary not found: ${BINARY}" >&2
    exit 1
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-mali}"
export OLDUVAI_NO_VSYNC=1
export OLDUVAI_SMOOTH_SUBFRAMES=2
export OLDUVAI_FRAME_STATS=1
export OLDUVAI_FORCE_SMOOTH=1
# Why FORCE_SMOOTH: a frame-capped run would otherwise disable smooth
# (boss_app.cpp:555 — smooth needs `max_frames <= 0 && shot.empty()`, or this
# override), so the measured config would silently fall back to subframes=1.00
# and read ~18 Hz instead of the spike's 2.00 / 11.2 Hz.  NO_VSYNC + a fixed
# sub-frame count keep it on the DISCRETE path, matching the shipped config.

GAME_DIR="${OLDUVAI_GAME_DATA:-$(dirname "${BINARY}")/game}"
ROM_DIR="$(dirname "${BINARY}")/mt32-roms"
[ -d "${ROM_DIR}" ] || ROM_DIR="$(dirname "${BINARY}")/roms"

for SEG in ${SEGMENTS}; do
    L="${SEG%v}"
    if [ "${SEG}" = "${L}" ]; then
        echo "== run_boss_measure level ${L} (frames=${FRAMES}) ==" >&2
        CAP="--play-frames ${FRAMES}"
        WIN=""
    else
        echo "== run_boss_measure level ${L} victory ==" >&2
        CAP=""
        if [ "${L}" = 4 ]; then WIN="OLDUVAI_FORCE_L4_RIDEOFF=20"
        else WIN="OLDUVAI_FORCE_WIN=20"; fi
        WIN="${WIN} OLDUVAI_STOP_AFTER_LEVEL=1"
    fi
    # shellcheck disable=SC2086
    env ${WIN} "${BINARY}" \
        --game-dir "${GAME_DIR}" \
        --rom-dir "${ROM_DIR}" \
        --enhanced --transitions smooth --profile hd \
        --aspect widescreen --render-scale 3 --hd-profile smooth \
        --play --level "${L}" ${CAP} ${BOSS_MEASURE_EXTRA:-} </dev/null
done