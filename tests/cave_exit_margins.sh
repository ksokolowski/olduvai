#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Cave transition margin symmetry — widescreen.
#
# THE BUG THIS PINS.  A cave has no horizontal neighbours, so in widescreen its
# margins are a clean black bezel.  That held on the way IN but not on the way
# OUT: the kind-2 fade computed `ws_old = wsp.present_path()` for the OUTGOING
# frame at a point where enter_cave/exit_cave had ALREADY flipped
# state.cave_flag and state.current_screen to the NEW side.  So on a cave EXIT
# the predicate mis-read the outgoing CAVE as a no-neighbour SURFACE screen and
# wrapped it with composed margins — one transition's worth of widescreen
# sprites appearing on the cave screen, on every regular cave, L1 through L7.
# Found by playtest; invisible to F5 (it is a transition frame, not a steady
# one) and invisible to every existing test: wide_transition covers cave ENTRY
# only, and cave_lerp inspects the draw log, not pixels.
#
# WHY AN INVARIANT AND NOT A GOLDEN HASH.  The rule "a cave is bezel in both
# directions" is what we actually mean; a hash would also fire on unrelated
# pixel changes and would need regenerating each time, which is how a gate stops
# being read.  This asserts the rule and is index-free: it counts how many
# kind-2 wide transition frames have pure-black margins.  The L3 screen-4
# roundtrip fixture produces four fade phases — surface-out, cave-in, cave-out,
# surface-in — so roughly half the frames must be bezel.  With the bug, the
# cave-out phase had content and the count collapsed (22 vs 40 measured).
#
# Skips (77) without game data, the binary, or Python 3 (used for the pixel
# read — the repo has no other image tooling dependency).

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(dirname "$0")/fixtures"
SKIP=77
MIN_BEZEL_FRAMES=35     # measured 40 after the fix, 22 with the bug

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "cave_exit_margins: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "cave_exit_margins: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "cave_exit_margins: SKIP — python3 not available"
    exit ${SKIP}
fi
python3 -c "import PIL" 2>/dev/null || {
    echo "cave_exit_margins: SKIP — python3 Pillow not available"
    exit ${SKIP}
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
OUT_DIR="$(mktemp -d /tmp/cave_exit_margins.XXXXXX)"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"

XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_WS_FORCE_MARGIN=64 \
    OLDUVAI_DUMP_TRANSITION="${OUT_DIR}" timeout 180 \
    "${BINARY}" --play --level 3 --start-screen 4 --enhanced \
    --hd-profile mmpx --aspect widescreen --transitions classic \
    --replay "${FIX}/l3s4_cave_roundtrip.jsonl" \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

RESULT=$(python3 - "${OUT_DIR}" "${MIN_BEZEL_FRAMES}" <<'PY'
import glob, os, sys
from PIL import Image
import numpy as np
out_dir, need = sys.argv[1], int(sys.argv[2])
frames = sorted(glob.glob(os.path.join(out_dir, "wtrans_k2_*.bmp")))
if not frames:
    print("FAIL no kind-2 wide transition frames produced")
    raise SystemExit
bezel = 0
for f in frames:
    im = np.asarray(Image.open(f).convert("RGB")).astype(int)
    h, w, _ = im.shape
    m = round(64 / 448 * w)                      # native margin -> output px
    if im[:, :m, :].mean() < 1.0 and im[:, w - m:, :].mean() < 1.0:
        bezel += 1
print(f"{'OK' if bezel >= need else 'FAIL'} {bezel} {len(frames)}")
PY
)

STATUS=$(echo "${RESULT}" | cut -d' ' -f1)
BEZEL=$(echo "${RESULT}" | cut -d' ' -f2)
TOTAL=$(echo "${RESULT}" | cut -d' ' -f3)

if [ "${STATUS}" = "OK" ]; then
    echo "cave_exit_margins: OK — ${BEZEL}/${TOTAL} kind-2 frames are bezel"
    rm -rf "${OUT_DIR}"
    exit 0
fi

echo "cave_exit_margins: FAIL — only ${BEZEL}/${TOTAL} kind-2 frames are bezel"
echo "  (need >= ${MIN_BEZEL_FRAMES}; frames kept at ${OUT_DIR})"
echo ""
echo "The cave side of a widescreen fade must be a pure black bezel in BOTH"
echo "directions.  Fewer bezel frames than expected means the outgoing cave"
echo "frame is being wrapped with composed margins again — check that ws_old"
echo "in game_app's kind-2 block still reflects the OUTGOING side"
echo "(was_cave/was_secret), not the post-exit_cave state."
exit 1
