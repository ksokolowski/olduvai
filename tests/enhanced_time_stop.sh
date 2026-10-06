#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Enhanced-only animations stop game time — invoked by CTest.
#
# The Enhanced teleport arrival (clouds, then the player) and the cave emerge
# are ticks the original does not have.  systems::run_tick holds the world
# for them (enhanced_fx_stops_time), so no timer, monster or key can act
# underneath: the regression was DOWN held at the L1 cave sign's arrival,
# which stands beside another cave's entrance — the cave descent started
# mid-clouds and the arrival played out inside the second cave.
#
# cave_l1_in holds RIGHT and DOWN from the start of L1 screen 2: it enters
# cave 102, the cave sign teleports the player out beside cave 103, and the
# held DOWN enters that one.  From the per-frame trace:
#   Classic   the arrival is instant (the EXE), cave 103 starts a few frames
#             after it.
#   Enhanced  the departure and the arrival's 15 ticks stop the frame counter
#             (the level timer's clock, a wrap already due included) and the
#             player first; cave 103 starts only after them.
#
# Skips (77) when game data, the binary or python3 is absent.
set -eu

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(cd "$(dirname "$0")/fixtures" && pwd)"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "enhanced_time_stop: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "enhanced_time_stop: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
command -v python3 >/dev/null 2>&1 || {
    echo "enhanced_time_stop: SKIP — python3 not available"; exit ${SKIP}; }

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

WORK=$(mktemp -d /tmp/enhanced_time_stop.XXXXXX)
trap 'rm -rf "${WORK}"' EXIT

run() {   # <trace> <extra args...>
    out="$1"; shift
    XDG_CONFIG_HOME="${WORK}/cfg" timeout 120 "${BINARY}" --no-config \
        --play --level 1 --start-screen 2 --transitions classic \
        --replay "${FIX}/cave_l1_in.jsonl" --play-frames 200 \
        --music-device off --game-dir "${GAME_DIR}" --trace "${out}" "$@" \
        >/dev/null 2>&1
}
run "${WORK}/classic.jsonl"
run "${WORK}/enhanced.jsonl" --enhanced --hd-profile mmpx --render-scale 2

python3 - "${WORK}/classic.jsonl" "${WORK}/enhanced.jsonl" <<'EOF'
import json, sys

def load(path):
    return [json.loads(line) for line in open(path)]

def exit_and_reentry(rows):
    """Index of the cave-102 -> surface teleport and of the next cave entry."""
    out = next((i for i in range(1, len(rows))
                if rows[i - 1]["screen"] == 102 and rows[i]["screen"] == 2), None)
    if out is None:
        return None, None
    back = next((j for j in range(out + 1, len(rows))
                 if rows[j]["cave_flag"]), None)
    return out, back

def stopped_run(rows, start):
    """Frames from `start` on in which the frame counter does not move."""
    n = 0
    while start + n + 1 < len(rows) and \
            rows[start + n + 1]["frame_counter"] == rows[start]["frame_counter"]:
        n += 1
    return n

fails = []
cls = load(sys.argv[1])
enh = load(sys.argv[2])

c_out, c_back = exit_and_reentry(cls)
e_out, e_back = exit_and_reentry(enh)
if c_out is None or c_back is None or e_out is None or e_back is None:
    print("enhanced_time_stop: FAIL — the replay did not reach the second cave "
          "(classic %s/%s, enhanced %s/%s)" % (c_out, c_back, e_out, e_back))
    sys.exit(1)

# Classic: the EXE has no arrival animation; held DOWN re-enters at once.
if c_back - c_out > 6:
    fails.append("classic re-entry took %d frames, want <= 6 (the EXE's)"
                 % (c_back - c_out))

# Enhanced: the whole arrival (15 ticks) plays before the descent can start.
if e_back - e_out < 18:
    fails.append("enhanced re-entry took %d frames, want >= 18 (15 arrival "
                 "ticks + the descent)" % (e_back - e_out))
held = stopped_run(enh, e_out)
if held < 14:
    fails.append("the frame counter held %d frames during the arrival, want "
                 ">= 14 (time stopped)" % held)
# The longest stop (the cave sign's departure and arrival) holds the level
# timer too: a wrap already due waits for the first tick that runs.
first, last, i = 0, 0, 0
while i < len(enh) - 1:
    j = i
    while j + 1 < len(enh) and \
            enh[j + 1]["frame_counter"] == enh[i]["frame_counter"]:
        j += 1
    if j - i > last - first:
        first, last = i, j
    i = j + 1
timers = {r["timer"] for r in enh[max(first - 1, 0):last + 1]}
if last - first < 24:
    fails.append("the cave sign's stop held the counter %d frames, want "
                 ">= 24 (departure + arrival)" % (last - first))
if len(timers) != 1:
    fails.append("the level timer moved during the stop: %s" % sorted(timers))
px = {r["player_x"] for r in enh[e_out:e_out + 14]}
if len(px) != 1:
    fails.append("the player moved during the arrival: x in %s" % sorted(px))

# The level start plays the same arrival: the clock waits for it.
start_held = stopped_run(enh, 0)
if start_held < 14:
    fails.append("the level start's arrival held the frame counter %d "
                 "frames, want >= 14" % start_held)
if stopped_run(cls, 0) != 0:
    fails.append("classic's frame counter stopped at the level start")

if fails:
    for f in fails:
        print("enhanced_time_stop: FAIL — " + f)
    sys.exit(1)
print("enhanced_time_stop: OK — re-entry after the exit: classic %d frames, "
      "enhanced %d (counter held %d)" % (c_back - c_out, e_back - e_out, held))
EOF
