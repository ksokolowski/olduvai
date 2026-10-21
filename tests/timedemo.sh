#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The timedemo runs flat out — invoked by CTest.
#
# OLDUVAI_UNTHROTTLED=1 with --replay drops every pacing wait
# (presentation/pacing.hpp) for benchmarks.  That it is the same run is held
# by the golden-trace corpus itself, which runs unthrottled
# (tests/lib/trace_gate.sh) against goldens recorded paced, two scenarios
# kept paced.  Here: an Enhanced replay with smooth motion (sub-frames,
# smooth transitions) must say it ran unthrottled and report a tick rate
# above the 18.2 Hz it is paced at, or nothing was unthrottled.
#
# Skips (77) when game data or the binary is absent.
set -u
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "timedemo: SKIP — game data not found at ${GAME_DIR}"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "timedemo: SKIP — binary not found: ${BINARY}"
    exit 77
fi
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export OLDUVAI_UNTHROTTLED=1
WORK=$(mktemp -d /tmp/timedemo.XXXXXX)
fail=0
trap '[ "${fail}" -eq 0 ] && rm -rf "${WORK}"' EXIT

# The rate: an Enhanced smooth replay, the frame-stats line.
OLDUVAI_FRAME_STATS=1 OLDUVAI_FORCE_SMOOTH=1 OLDUVAI_PANEL=1280x720 \
    XDG_CONFIG_HOME="${WORK}/cfg" timeout 300 "${BINARY}" --no-config \
    --default-profile hd-handheld --window 1280x720 --play --level 1 \
    --music-device off --game-dir "${GAME_DIR}" \
    --replay "${HERE}/fixtures/walk_in.jsonl" > "${WORK}/rate.log" 2>&1
grep -q 'timedemo: unthrottled replay' "${WORK}/rate.log" || {
    echo "FAIL: the replay did not run unthrottled"; fail=1; }
hz=$(sed -n 's/.*eff_hz=\([0-9.]*\).*/\1/p' "${WORK}/rate.log" | head -1)
if [ -n "${hz}" ] && awk "BEGIN { exit !(${hz} > 18.21) }"; then
    echo "  Enhanced smooth replay: ${hz} ticks/s"
else
    echo "FAIL: eff_hz '${hz}' is not above 18.2"; fail=1
fi
# engine_hz leaves the presents' waits out, so it is at least eff_hz.
ehz=$(sed -n 's/.*engine_hz=\([0-9.]*\).*/\1/p' "${WORK}/rate.log" | head -1)
if [ -n "${ehz}" ] && [ -n "${hz}" ] && awk "BEGIN { exit !(${ehz} >= ${hz}) }"; then
    echo "  engine_hz ${ehz}"
else
    echo "FAIL: engine_hz '${ehz}' missing or below eff_hz '${hz}'"; fail=1
fi
[ "${fail}" -eq 0 ] && echo "timedemo: OK"
exit "${fail}"
