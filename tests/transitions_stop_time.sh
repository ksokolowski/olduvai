#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Enhanced presentation sequences run no game ticks — invoked by CTest.
#
# The smooth screen pan, the fades, the secret-room slide and the L3 trunk
# descent are blocking playbacks between ticks.  No game time may pass during
# them: then a key held through one is held at the next tick, as after the
# original's own blocking fades, and nothing acts underneath (the class of
# bug enhanced_time_stop pins for the Enhanced-only ticks).
#
# Each replay runs twice in Enhanced, transitions smooth (OLDUVAI_FORCE_SMOOTH
# keeps them under --trace, which otherwise forces classic, and says so) and
# classic (instant); the per-tick traces must be identical.  The smooth run
# must not have been forced classic, and each replay must change screen, so
# the comparison cannot pass vacuously.  (Present counts do not tell the two
# apart: a secret room presents more in classic.)
#
# Skips (77) when game data or the binary is absent.
set -eu
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(cd "$(dirname "$0")/fixtures" && pwd)"
SKIP=77
if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "transitions_stop_time: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "transitions_stop_time: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK=$(mktemp -d /tmp/transitions_stop_time.XXXXXX)
trap 'rm -rf "${WORK}"' EXIT

run() {   # <out> <transitions> <force smooth> <level> <screen|-> <fixture> <frames>
    out="$1" tr="$2" force="$3" level="$4" ss="$5" fx="$6" frames="$7"
    set -- --level "$level"
    [ "$ss" = "-" ] || set -- "$@" --start-screen "$ss"
    OLDUVAI_FORCE_SMOOTH="$force" XDG_CONFIG_HOME="${WORK}/cfg" timeout 300 "${BINARY}" --no-config \
        --play "$@" --enhanced --hd-profile mmpx --render-scale 2 \
        --transitions "$tr" --replay "${FIX}/${fx}" --play-frames "$frames" \
        --music-device off --game-dir "${GAME_DIR}" --trace "${out}.jsonl" \
        > "${out}.log" 2>&1 || true
}

fail=0
for s in "walk 1 - walk_in.jsonl 400" "secret 1 5 secret_l1_in.jsonl 300" \
         "cave 1 2 cave_l1_in.jsonl 300" "trunk 5 9 l5s9_trunk_warp.jsonl 300"; do
    # shellcheck disable=SC2086  # the scenario is split into its fields
    set -- $s
    name="$1"
    run "${WORK}/${name}.smooth" smooth 1 "$2" "$3" "$4" "$5" &
    run "${WORK}/${name}.classic" classic 0 "$2" "$3" "$4" "$5" &
    wait
    a="${WORK}/${name}.smooth.jsonl" b="${WORK}/${name}.classic.jsonl"
    if [ ! -s "$a" ] || [ ! -s "$b" ]; then
        echo "FAIL: ${name}: no trace"; fail=1; continue
    fi
    if grep -q "forcing transitions classic" "${WORK}/${name}.smooth.log"; then
        echo "FAIL: ${name}: the smooth run was forced classic"; fail=1
    fi
    screens=$(sed -n 's/.*"screen": *\([0-9]*\).*/\1/p' "$a" | sort -u | wc -l)
    [ "$screens" -ge 2 ] || {
        echo "FAIL: ${name}: the replay stays on one screen"; fail=1; }
    if cmp -s "$a" "$b"; then
        echo "  ${name}: $(wc -l < "$a" | tr -d ' ') ticks identical, ${screens} screens"
    else
        echo "FAIL: ${name}: smooth and classic traces differ ($(cmp "$a" "$b" 2>&1 | head -1))"
        fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "transitions_stop_time: OK"
exit "$fail"
