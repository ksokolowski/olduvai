#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Smooth-motion cave-enter snap — invoked by CTest.
#
# Regression net for bug report 2026-07-17_165051_L3_S4: entering the L3
# (internal 5) screen-4 cave teleports the player only (9,11) px — UNDER the
# 16-px lerp snap threshold — so the sub-frame interpolation swept the sprite
# from the surface hole onto the cave background for one tick.  The fix snaps
# on the screen/cave-mode change signal instead of distance alone.
#
# Replays a scripted ROUND TRIP (surface → cave → back to the surface) under
# --enhanced (smooth motion) with OLDUVAI_DRAW_LOG, then asserts both
# teleport ticks — cave entry AND surface return, each a sub-16px hop — draw
# every sub-frame at the same position: no in-between glide points.
#
# Skips (77) when game data or the binary is absent.
set -eu

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(cd "$(dirname "$0")/fixtures" && pwd)"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "cave_lerp: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "cave_lerp: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
command -v python3 >/dev/null 2>&1 || {
    echo "cave_lerp: SKIP — python3 not available"; exit ${SKIP}; }

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

WORK=$(mktemp -d /tmp/cave_lerp.XXXXXX)
trap 'rm -rf "${WORK}"' EXIT

OLDUVAI_DRAW_LOG="${WORK}/draw.jsonl" timeout 120 \
    "${BINARY}" --play --level 3 --start-screen 4 --enhanced \
    --replay "${FIX}/l3s4_cave_roundtrip.jsonl" \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1

python3 - "${WORK}/draw.jsonl" <<'EOF'
import json, sys
from collections import defaultdict

CAVE_ENTRY = (8, 139)      # enter_cave landing position for this cave
SURFACE_RETURN = (17, 150) # exit_cave return position (the entrance)

frames = defaultdict(list)
for line in open(sys.argv[1]):
    r = json.loads(line)
    frames[r["f"]].append((r["px"], r["py"]))

if not frames:
    print("cave_lerp: FAIL — draw log is empty")
    sys.exit(1)

# A teleport tick is the first frame whose FINAL sub-frame lands within a
# few px of the target — pre-fix the glide's last sub stops just short of
# it (alpha < 1), so an exact match would skip past the buggy frame to the
# first steady frame and false-pass.  Fall frames land far away.  The
# surface-return search starts AFTER the entry frame: the walk TO the
# entrance parks on the same position before entering.
def near(p, q, r=6):
    return abs(p[0] - q[0]) <= r and abs(p[1] - q[1]) <= r

def find_tick(target, after, label):
    for f in sorted(frames):
        if f > after and near(frames[f][-1], target):
            return f
    print(f"cave_lerp: FAIL — replay never reached the {label} position "
          f"{target}; fixture or route regressed")
    sys.exit(1)

def assert_snap(f, label):
    subs = frames[f]
    if len(set(subs)) != 1:
        print(f"cave_lerp: FAIL — {label} frame {f} glides across the "
              f"screen change instead of snapping: subs={subs}")
        sys.exit(1)
    return len(subs)

entry = find_tick(CAVE_ENTRY, after=-1, label="cave entry")
n1 = assert_snap(entry, "cave-entry teleport")
ret = find_tick(SURFACE_RETURN, after=entry, label="surface return")
n2 = assert_snap(ret, "surface-return teleport")

print(f"cave_lerp: OK — entry frame {entry} ({n1} subs) and return frame "
      f"{ret} ({n2} subs) both snap")
sys.exit(0)
EOF
