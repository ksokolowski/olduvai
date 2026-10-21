#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Ticks per second the engine reaches on fixed replays, run flat out (the
# timedemo, OLDUVAI_UNTHROTTLED).  Report-only, for a before / after across a
# change: run it on both builds, on the same machine, and compare.
#
#   scripts/metrics/timedemo_rate.sh [binary] [game_dir] [runs]
#
# Cases: L1 full clear Classic (one present a tick) and Enhanced with smooth
# motion (its presents and transitions), at the handheld profile on a 720p
# panel; the L2 boss fight Enhanced smooth.  Each prints, as medians of
# `runs` (default 3): eff_hz, the rate as shown (on a handheld, capped by the
# panel's page flip), and engine_hz, the same without the presents' waits.
#
# Where it is wrong: wall time on a busy machine wobbles (close other work;
# compare medians, not single runs), and level start is outside it.
# engine_hz is approximate: a present's time also holds the GPU finishing
# its work (left out), and under a flip cap each wait costs a little wake-up
# time (left in): with an emulated 60 Hz cap it read 17% low in Enhanced.
# No audio in these runs.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd -P)
BIN="${1:-${REPO}/build/release/olduvai}"
GAME="${2:-${OLDUVAI_GAME_DATA:-${REPO}/game_data}}"
RUNS="${3:-3}"
FIX="${REPO}/tests/fixtures"
[ -x "${BIN}" ] || { echo "timedemo_rate: no binary at ${BIN}" >&2; exit 2; }
[ -f "${GAME}/FILESA.VGA" ] || { echo "timedemo_rate: no game files at ${GAME}" >&2; exit 2; }
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export OLDUVAI_UNTHROTTLED=1 OLDUVAI_FRAME_STATS=1 OLDUVAI_PANEL=1280x720
WORK=$(mktemp -d "${TMPDIR:-/tmp}/timedemo_rate.XXXXXX")
trap 'rm -rf "${WORK}"' EXIT

median() {   # <file>: "median (min, max)" of one number per line
    sort -n "$1" | awk '{ v[NR] = $1 }
        END { if (NR == 0) { printf "none"; exit }
              printf "%.1f (%.1f-%.1f)", v[int((NR + 1) / 2)], v[1], v[NR] }'
}

rate() {   # <label> <env> <args...>: eff_hz and engine_hz medians of RUNS runs
    label="$1" envs="$2"; shift 2
    [ "${envs}" = "-" ] && envs=""
    : > "${WORK}/hz"; : > "${WORK}/ehz"
    i=0
    while [ "${i}" -lt "${RUNS}" ]; do
        # shellcheck disable=SC2086  # envs is a word list
        env XDG_CONFIG_HOME="${WORK}/cfg" ${envs} "${BIN}" --no-config \
            --game-dir "${GAME}" --window 1280x720 --play "$@" \
            > "${WORK}/log" 2>&1
        sed -n 's/.*eff_hz=\([0-9.]*\).*/\1/p' "${WORK}/log" | head -1 \
            >> "${WORK}/hz"
        sed -n 's/.*engine_hz=\([0-9.]*\).*/\1/p' "${WORK}/log" | head -1 \
            >> "${WORK}/ehz"
        i=$((i + 1))
    done
    printf "%-32s eff_hz %s  engine_hz %s\n" "${label}" \
        "$(median "${WORK}/hz")" "$(median "${WORK}/ehz")"
}

rate "L1 full clear, Classic" - --profile dos --level 1 \
    --replay "${FIX}/l1_full_clear.jsonl" --play-frames 2740 \
    --transitions classic
rate "L1 full clear, Enhanced smooth" OLDUVAI_FORCE_SMOOTH=1 \
    --default-profile hd-handheld --level 1 \
    --replay "${FIX}/l1_full_clear.jsonl"
rate "L2 boss fight, Enhanced smooth" OLDUVAI_FORCE_SMOOTH=1 \
    --default-profile hd-handheld --level 2 \
    --replay "${FIX}/l2_boss_fight.jsonl"
